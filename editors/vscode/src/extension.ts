// Luna Language Extension for VS Code v1.7.0
// Pure Luna Engine - 100% native LSP via lsp.luna
// No Rust FFI dependencies - the Luna binary handles all JSON-RPC directly

import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;
let outputChannel: vscode.OutputChannel;
let statusBarItem: vscode.StatusBarItem;

export function activate(context: vscode.ExtensionContext) {
    outputChannel = vscode.window.createOutputChannel('Luna');
    outputChannel.appendLine('🌙 Luna v4.2 Extension activated');
    outputChannel.appendLine('   Engine: Pure Luna (lsp.luna)');
    outputChannel.appendLine('   Protocol: JSON-RPC 2.0 over stdio');

    // Create status bar item
    statusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
    statusBarItem.text = '$(moon) Luna';
    statusBarItem.tooltip = 'Luna Language Server';
    statusBarItem.command = 'luna.showOutput';
    context.subscriptions.push(statusBarItem);

    // Start LSP if enabled
    const config = vscode.workspace.getConfiguration('luna');
    if (config.get<boolean>('lsp.enabled', true)) {
        startLspClient(context);
    }

    // Register commands
    context.subscriptions.push(
        vscode.commands.registerCommand('luna.runFile', runCurrentFile),
        vscode.commands.registerCommand('luna.restartLsp', () => restartLspClient(context)),
        vscode.commands.registerCommand('luna.buildFile', buildCurrentFile),
        vscode.commands.registerCommand('luna.showOutput', () => outputChannel.show()),
        vscode.commands.registerCommand('luna.checkFile', checkCurrentFile),
        vscode.commands.registerCommand('luna.formatFile', formatCurrentFile)
    );

    // Watch for configuration changes
    context.subscriptions.push(
        vscode.workspace.onDidChangeConfiguration(e => {
            if (e.affectsConfiguration('luna.lsp.enabled')) {
                const enabled = vscode.workspace.getConfiguration('luna').get<boolean>('lsp.enabled', true);
                if (enabled && !client) {
                    startLspClient(context);
                } else if (!enabled && client) {
                    stopLspClient();
                }
            }
        })
    );

    // Show status bar when Luna file is active
    context.subscriptions.push(
        vscode.window.onDidChangeActiveTextEditor(editor => {
            if (editor && editor.document.languageId === 'luna') {
                statusBarItem.show();
            } else {
                statusBarItem.hide();
            }
        })
    );

    // Check current editor
    if (vscode.window.activeTextEditor?.document.languageId === 'luna') {
        statusBarItem.show();
    }
}

function startLspClient(context: vscode.ExtensionContext) {
    const config = vscode.workspace.getConfiguration('luna');
    const lunaPath = config.get<string>('path', 'luna');
    const trace = config.get<boolean>('lsp.trace', false);

    outputChannel.appendLine(`\n[LSP] Starting Pure Luna Language Server`);
    outputChannel.appendLine(`[LSP] Command: ${lunaPath} lsp`);
    outputChannel.appendLine(`[LSP] Transport: stdio (JSON-RPC 2.0)`);

    // Server options - spawn luna lsp (native Luna binary)
    // The LSP server is implemented in lsp.luna and handles:
    // - textDocument/completion (IntelliSense for 31 stdlib modules)
    // - textDocument/hover (documentation from stdlib)
    // - textDocument/definition (go-to-definition)
    // - textDocument/references (find all references)
    // - textDocument/diagnostics (borrow_checker.luna errors)
    // - textDocument/formatting (code formatting)
    const serverOptions: ServerOptions = {
        command: lunaPath,
        args: ['lsp'],
        transport: TransportKind.stdio,
        options: {
            env: {
                ...process.env,
                LUNA_LSP_MODE: 'native'  // Signal pure Luna mode
            }
        }
    };

    // Client options with enhanced capabilities
    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'luna' }],
        outputChannel: outputChannel,
        traceOutputChannel: trace ? outputChannel : undefined,
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.luna')
        },
        initializationOptions: {
            // Luna v4.2 specific options
            enableBorrowChecker: true,
            enableTypeInference: true,
            stdlib: {
                // All 31 native modules
                modules: [
                    'core', 'io', 'string', 'math', 'time', 'fs',
                    'json', 'regex', 'base64', 'uuid',
                    'vec', 'hashmap', 'hashset', 'btree', 'heap',
                    'crypto', 'hash',
                    'http', 'net', 'websocket', 'url',
                    'sync', 'thread', 'atomic',
                    'db', 'sql',
                    'env', 'process', 'os',
                    'fmt', 'log'
                ]
            },
            cosmicKeywords: ['shine', 'eclipse', 'orbit', 'phase', 'spawn', 'seal', 'nova', 'guard', 'meow']
        },
        middleware: {
            // Enhanced diagnostics from borrow_checker.luna
            handleDiagnostics: (uri, diagnostics, next) => {
                // Tag diagnostics with source
                const taggedDiagnostics = diagnostics.map(d => ({
                    ...d,
                    source: d.source || 'luna'
                }));
                next(uri, taggedDiagnostics);
            }
        }
    };

    // Create and start the client
    client = new LanguageClient(
        'lunaLanguageServer',
        'Luna Language Server (Pure Luna)',
        serverOptions,
        clientOptions
    );

    // Update status bar during startup
    statusBarItem.text = '$(sync~spin) Luna';
    statusBarItem.tooltip = 'Luna LSP: Starting...';

    client.start().then(() => {
        outputChannel.appendLine('[LSP] ✓ Luna Language Server started successfully');
        outputChannel.appendLine('[LSP] ✓ Pure Luna engine ready (lsp.luna)');
        outputChannel.appendLine('[LSP] ✓ Borrow checker diagnostics enabled');
        outputChannel.appendLine('[LSP] ✓ IntelliSense for 31 stdlib modules');

        statusBarItem.text = '$(moon) Luna';
        statusBarItem.tooltip = 'Luna LSP: Ready (Pure Luna Engine)';

        vscode.window.setStatusBarMessage('Luna Language Server ready', 3000);
    }).catch((error) => {
        outputChannel.appendLine(`[LSP] ✗ Failed to start: ${error}`);

        statusBarItem.text = '$(error) Luna';
        statusBarItem.tooltip = 'Luna LSP: Error';

        vscode.window.showErrorMessage(
            `Failed to start Luna Language Server.\n\n` +
            `Make sure 'luna' is installed and in your PATH.\n` +
            `Run 'luna --install-system' to set up Luna system-wide.\n\n` +
            `Error: ${error}`
        );
    });

    context.subscriptions.push(client);
}

async function stopLspClient() {
    if (client) {
        await client.stop();
        client = undefined;
        outputChannel.appendLine('[LSP] Luna Language Server stopped');
        statusBarItem.text = '$(moon) Luna';
        statusBarItem.tooltip = 'Luna LSP: Stopped';
    }
}

async function restartLspClient(context: vscode.ExtensionContext) {
    outputChannel.appendLine('[LSP] Restarting Luna Language Server...');
    statusBarItem.text = '$(sync~spin) Luna';
    await stopLspClient();
    startLspClient(context);
}

async function runCurrentFile() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        vscode.window.showWarningMessage('No active Luna file');
        return;
    }

    const document = editor.document;
    if (document.languageId !== 'luna') {
        vscode.window.showWarningMessage('Current file is not a Luna file');
        return;
    }

    // Save the file first
    await document.save();

    const config = vscode.workspace.getConfiguration('luna');
    const lunaPath = config.get<string>('path', 'luna');
    const filePath = document.fileName;

    outputChannel.appendLine(`\n[Run] Executing: ${filePath}`);

    // Create terminal and run with JIT (default mode)
    const terminal = vscode.window.createTerminal({
        name: 'Luna',
        iconPath: new vscode.ThemeIcon('moon')
    });
    terminal.show();
    terminal.sendText(`${lunaPath} "${filePath}"`);
}

async function buildCurrentFile() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        vscode.window.showWarningMessage('No active Luna file');
        return;
    }

    const document = editor.document;
    if (document.languageId !== 'luna') {
        vscode.window.showWarningMessage('Current file is not a Luna file');
        return;
    }

    // Save the file first
    await document.save();

    const config = vscode.workspace.getConfiguration('luna');
    const lunaPath = config.get<string>('path', 'luna');
    const filePath = document.fileName;

    outputChannel.appendLine(`\n[Build] Compiling: ${filePath}`);
    outputChannel.show();

    // Create terminal and build to native binary (AOT)
    const terminal = vscode.window.createTerminal({
        name: 'Luna Build',
        iconPath: new vscode.ThemeIcon('tools')
    });
    terminal.show();
    terminal.sendText(`${lunaPath} build "${filePath}"`);
}

async function checkCurrentFile() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        vscode.window.showWarningMessage('No active Luna file');
        return;
    }

    const document = editor.document;
    if (document.languageId !== 'luna') {
        vscode.window.showWarningMessage('Current file is not a Luna file');
        return;
    }

    // Save the file first
    await document.save();

    const config = vscode.workspace.getConfiguration('luna');
    const lunaPath = config.get<string>('path', 'luna');
    const filePath = document.fileName;

    outputChannel.appendLine(`\n[Check] Type checking: ${filePath}`);
    outputChannel.show();

    // Run type check only (no execution)
    const terminal = vscode.window.createTerminal({
        name: 'Luna Check',
        iconPath: new vscode.ThemeIcon('check')
    });
    terminal.show();
    terminal.sendText(`${lunaPath} check "${filePath}"`);
}

async function formatCurrentFile() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        vscode.window.showWarningMessage('No active Luna file');
        return;
    }

    const document = editor.document;
    if (document.languageId !== 'luna') {
        vscode.window.showWarningMessage('Current file is not a Luna file');
        return;
    }

    // Use LSP formatting if available
    if (client) {
        await vscode.commands.executeCommand('editor.action.formatDocument');
        outputChannel.appendLine(`[Format] Formatted: ${document.fileName}`);
    } else {
        vscode.window.showWarningMessage('Luna LSP not running. Enable it for formatting support.');
    }
}

export function deactivate(): Thenable<void> | undefined {
    outputChannel.appendLine('🌙 Luna extension deactivating...');
    if (client) {
        return client.stop();
    }
    return undefined;
}
