#pragma once

namespace prism::mcp {

// Attach to a running Prism MCP server over stdio (Content-Length framed JSON-RPC).
// Returns a process exit code. Does not start the GUI.
int runStdioAttach();

} // namespace prism::mcp
