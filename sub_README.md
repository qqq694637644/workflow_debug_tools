实现workflow脚本语言的的调试功能,所有代码由chatgpt5.4mini xhigh开发
架构,vscode起服务端,脚本语言加载调试库,远程接入调试



vscode调试配置
{
  "version": "0.2.0",
    {
      "name": "Workflow mytest",
      "type": "workflow",
      "request": "attach",
      "host": "127.0.0.1",
      "port": 4711,
      "workspaceRoot": "${workspaceFolder}\\Test\\UnitTest\\mytest",
      "connectTimeoutMs": 0,
      "stopOnEntry": true,
      "pathMapping": []
    }
  ]
}

