#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def find_solution_root(start_dir: Path) -> Path:
	current = start_dir.resolve()
	for candidate in (current, *current.parents):
		if (candidate / "Test" / "UnitTest" / "UnitTest.sln").exists():
			return candidate / "Test" / "UnitTest"
	raise FileNotFoundError("找不到 Test\\UnitTest\\UnitTest.sln，请确认当前目录在仓库内。")


def find_vsdevcmd() -> Path:
	# 优先使用环境变量；找不到时再从常见的 VS 安装目录里搜。
	env_path = os.environ.get("VLPP_VSDEVCMD_PATH")
	if env_path:
		path = Path(env_path)
		if path.is_file():
			return path
		raise FileNotFoundError(f"VLPP_VSDEVCMD_PATH 指向的文件不存在: {path}")

	candidates = []
	for program_files in (
		os.environ.get("ProgramFiles", r"C:\Program Files"),
		os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
	):
		vs_root = Path(program_files) / "Microsoft Visual Studio" / "2022"
		if vs_root.exists():
			candidates.extend(vs_root.glob("*/Common7/Tools/VsDevCmd.bat"))

	if candidates:
		return sorted(candidates, key=lambda path: str(path))[0]

	raise FileNotFoundError("找不到 VsDevCmd.bat，请设置 VLPP_VSDEVCMD_PATH。")


def load_vsdev_environment(vsdevcmd: Path) -> dict[str, str]:
	# 先执行一次 VsDevCmd.bat，再把它输出的环境变量读回来，后续直接调用 MSBuild.exe。
	with tempfile.NamedTemporaryFile("w", delete=False, suffix=".cmd", encoding="utf-8", newline="\r\n") as temp_file:
		temp_file.write("@echo off\r\n")
		temp_file.write(f'call "{vsdevcmd}"\r\n')
		temp_file.write("set\r\n")
		temp_batch = Path(temp_file.name)

	try:
		result = subprocess.run(
			["cmd.exe", "/d", "/s", "/c", str(temp_batch)],
			capture_output=True,
			text=True,
			encoding="utf-8",
			errors="replace",
		)
	finally:
		temp_batch.unlink(missing_ok=True)

	if result.returncode != 0:
		raise RuntimeError(result.stderr.strip() or result.stdout.strip() or "执行 VsDevCmd.bat 失败。")

	env = os.environ.copy()
	for raw_line in result.stdout.splitlines():
		if "=" not in raw_line:
			continue
		key, value = raw_line.split("=", 1)
		if key:
			env[key] = value
	return env


def get_env_value(env: dict[str, str], name: str) -> str | None:
	# Windows 环境变量名本来就是大小写不敏感的，这里按实际键名做一次兼容。
	for key, value in env.items():
		if key.lower() == name.lower():
			return value
	return None


def find_msbuild(env: dict[str, str]) -> Path:
	path_value = get_env_value(env, "PATH")
	if path_value:
		found = shutil.which("MSBuild.exe", path=path_value)
		if found:
			return Path(found)

	# 如果 PATH 里没搜到，就退回到 vswhere，避免被当前机器的环境变量污染卡住。
	for program_files in (
		os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
		os.environ.get("ProgramFiles", r"C:\Program Files"),
	):
		vswhere = Path(program_files) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
		if not vswhere.exists():
			continue

		query = [
			str(vswhere),
			"-latest",
			"-products",
			"*",
			"-requires",
			"Microsoft.Component.MSBuild",
			"-find",
			r"MSBuild\**\Bin\MSBuild.exe",
		]
		result = subprocess.run(query, capture_output=True, text=True, check=True)
		for raw_line in result.stdout.splitlines():
			candidate = Path(raw_line.strip())
			if candidate.is_file():
				return candidate

	raise FileNotFoundError("找不到 MSBuild.exe，请确认 Visual Studio 安装完整。")


def build_project(
	solution_root: Path,
	vsdevcmd: Path,
	project_name: str,
	configuration: str,
	platform: str,
	rebuild: bool,
) -> int:
	project_dir = solution_root / project_name
	project_file = project_dir / f"{project_name}.vcxproj"
	if not project_file.exists():
		raise FileNotFoundError(f"找不到项目文件: {project_file}")

	build_root = solution_root / ".build" / project_name / configuration / platform
	int_dir = build_root / "obj"

	# 默认只做增量编译，不执行 Rebuild。
	target = "Rebuild" if rebuild else "Build"
	msbuild_args = [
		"MSBuild.exe",
		str(project_file),
		"/m:8",
		f"/t:{target}",
		f"/p:Configuration={configuration};Platform={platform}",
		f"/p:OutDir={str(build_root.resolve())}\\",
		f"/p:IntDir={str(int_dir.resolve())}\\",
	]
	print(f"正在执行: {project_file.name} ({target}) {configuration}|{platform}")
	env = load_vsdev_environment(vsdevcmd)
	msbuild_args[0] = str(find_msbuild(env))
	result = subprocess.run(
		msbuild_args,
		cwd=project_dir,
		env=env,
	)
	return result.returncode


def sync_output(solution_root: Path, project_name: str, configuration: str, platform: str) -> None:
	# 复制到解决方案根目录，兼容现有的执行脚本。
	source_dir = solution_root / ".build" / project_name / configuration / platform
	target_dir = solution_root / platform / configuration
	source_exe = source_dir / f"{project_name}.exe"
	source_pdb = source_dir / f"{project_name}.pdb"

	if not source_exe.exists():
		raise FileNotFoundError(f"找不到构建产物: {source_exe}")

	target_dir.mkdir(parents=True, exist_ok=True)
	shutil.copy2(source_exe, target_dir / source_exe.name)
	if source_pdb.exists():
		shutil.copy2(source_pdb, target_dir / source_pdb.name)


def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser(description="Workflow 单元测试项目增量编译脚本")
	parser.add_argument("--project", default="RuntimeTest", help="要编译的项目名，默认 RuntimeTest")
	parser.add_argument(
		"--configuration",
		default="Debug",
		choices=("Debug", "Release"),
		help="编译配置，默认 Debug",
	)
	parser.add_argument(
		"--platform",
		default="x64",
		choices=("x64", "Win32"),
		help="编译平台，默认 x64",
	)
	parser.add_argument(
		"--rebuild",
		action="store_true",
		help="显式执行 Rebuild。默认不使用，以便保持增量编译。",
	)
	return parser.parse_args()


def main() -> int:
	args = parse_args()
	solution_root = find_solution_root(Path.cwd())
	vsdevcmd = find_vsdevcmd()

	exit_code = build_project(
		solution_root=solution_root,
		vsdevcmd=vsdevcmd,
		project_name=args.project,
		configuration=args.configuration,
		platform=args.platform,
		rebuild=args.rebuild,
	)
	if exit_code != 0:
		return exit_code

	sync_output(
		solution_root=solution_root,
		project_name=args.project,
		configuration=args.configuration,
		platform=args.platform,
	)
	print("编译完成，输出已同步。")
	return 0


if __name__ == "__main__":
	sys.exit(main())
