export function formatDebugMessage(message: unknown): string {
  if (message instanceof Error) {
    return message.stack ?? `${message.name}: ${message.message}`;
  }

  if (typeof message === 'string') {
    return message;
  }

  try {
    return JSON.stringify(message);
  }
  catch {
    return String(message);
  }
}
