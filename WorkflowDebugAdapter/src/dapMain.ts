import { WorkflowDebugDapServer } from './dapServer.js';

const server = new WorkflowDebugDapServer();
await server.run();
