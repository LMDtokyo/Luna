// Luna VS Code extension.
//
// Provides syntax highlighting, icons and snippets through the extension
// manifest (declarative), plus runtime integration with the Luna toolchain:
//   - Run / Build / Check commands that shell out to the `luna` binary.
//   - Language-server client that talks to `luna lsp` over stdio when the
//     binary is available.  If the binary is missing, the client is not
//     started and only the static features remain active.

import * as vscode from "vscode";
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
} from "vscode-languageclient/node";
import * as path from "path";
import * as fs from "fs";
import { execFile } from "child_process";

let client: LanguageClient | undefined;
let outputChannel: vscode.OutputChannel;
let statusBarItem: vscode.StatusBarItem;

export function activate(context: vscode.ExtensionContext): void {
    outputChannel = vscode.window.createOutputChannel("Luna");

    statusBarItem = vscode.window.createStatusBarItem(
        vscode.StatusBarAlignment.Left,
        100,
    );
    statusBarItem.text = "Luna";
    statusBarItem.tooltip = "Luna language";
    statusBarItem.command = "luna.showOutput";
    context.subscriptions.push(statusBarItem);

    context.subscriptions.push(
        vscode.commands.registerCommand("luna.runFile", runCurrentFile),
        vscode.commands.registerCommand("luna.buildFile", buildCurrentFile),
        vscode.commands.registerCommand("luna.checkFile", checkCurrentFile),
        vscode.commands.registerCommand("luna.formatFile", formatCurrentFile),
        vscode.commands.registerCommand("luna.restartLsp", () => restartLspClient(context)),
        vscode.commands.registerCommand("luna.showOutput", () => outputChannel.show()),
    );

    context.subscriptions.push(
        vscode.workspace.onDidChangeConfiguration((e) => {
            if (e.affectsConfiguration("luna.lsp.enabled")) {
                const enabled = vscode.workspace
                    .getConfiguration("luna")
                    .get<boolean>("lsp.enabled", true);
                if (enabled && !client) {
                    void startLspClient(context);
                } else if (!enabled && client) {
                    void stopLspClient();
                }
            }
        }),
    );

    context.subscriptions.push(
        vscode.window.onDidChangeActiveTextEditor((editor) => {
            if (editor && editor.document.languageId === "luna") {
                statusBarItem.show();
            } else {
                statusBarItem.hide();
            }
        }),
    );

    if (vscode.window.activeTextEditor?.document.languageId === "luna") {
        statusBarItem.show();
    }

    const config = vscode.workspace.getConfiguration("luna");
    if (config.get<boolean>("lsp.enabled", true)) {
        void startLspClient(context);
    }
}

async function startLspClient(context: vscode.ExtensionContext): Promise<void> {
    const config = vscode.workspace.getConfiguration("luna");
    const lunaPath = config.get<string>("path", "luna");
    const trace = config.get<boolean>("lsp.trace", false);

    const resolved = await resolveExecutable(lunaPath);
    if (!resolved) {
        outputChannel.appendLine(
            `[lsp] skipped: ${lunaPath} not found on PATH. ` +
                `Install the Luna toolchain to enable the language server.`,
        );
        statusBarItem.tooltip = "Luna language (LSP disabled — toolchain not found)";
        return;
    }

    outputChannel.appendLine(`[lsp] launching ${resolved} lsp`);

    const serverOptions: ServerOptions = {
        command: resolved,
        args: ["lsp"],
        transport: TransportKind.stdio,
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: "file", language: "luna" }],
        outputChannel: outputChannel,
        traceOutputChannel: trace ? outputChannel : undefined,
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher("**/*.luna"),
        },
    };

    client = new LanguageClient(
        "lunaLanguageServer",
        "Luna Language Server",
        serverOptions,
        clientOptions,
    );

    statusBarItem.text = "$(sync~spin) Luna";
    try {
        await client.start();
        statusBarItem.text = "Luna";
        statusBarItem.tooltip = "Luna language server: ready";
        outputChannel.appendLine("[lsp] server started");
    } catch (error) {
        statusBarItem.text = "Luna";
        statusBarItem.tooltip = "Luna language server: not running";
        outputChannel.appendLine(`[lsp] failed to start: ${error}`);
        client = undefined;
    }

    if (client) {
        context.subscriptions.push(client);
    }
}

async function stopLspClient(): Promise<void> {
    if (client) {
        await client.stop();
        client = undefined;
        outputChannel.appendLine("[lsp] server stopped");
        statusBarItem.tooltip = "Luna language server: stopped";
    }
}

async function restartLspClient(context: vscode.ExtensionContext): Promise<void> {
    outputChannel.appendLine("[lsp] restarting");
    await stopLspClient();
    await startLspClient(context);
}

function resolveExecutable(name: string): Promise<string | undefined> {
    // Absolute or relative path — check directly.
    if (path.isAbsolute(name) || name.includes(path.sep) || name.includes("/")) {
        return Promise.resolve(fs.existsSync(name) ? name : undefined);
    }
    // PATH lookup via `where` on Windows, `which` elsewhere.
    const tool = process.platform === "win32" ? "where" : "which";
    return new Promise((resolve) => {
        execFile(tool, [name], (err, stdout) => {
            if (err || !stdout) {
                resolve(undefined);
                return;
            }
            const first = stdout.toString().split(/\r?\n/).find((l) => l.trim().length > 0);
            resolve(first ? first.trim() : undefined);
        });
    });
}

async function runInTerminal(command: string, args: string[], terminalName: string): Promise<void> {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        vscode.window.showWarningMessage("No active Luna file");
        return;
    }
    if (editor.document.languageId !== "luna") {
        vscode.window.showWarningMessage("Current file is not a Luna file");
        return;
    }
    await editor.document.save();

    const filePath = editor.document.fileName;
    const config = vscode.workspace.getConfiguration("luna");
    const lunaPath = config.get<string>("path", "luna");

    const terminal = vscode.window.createTerminal({ name: terminalName });
    terminal.show();
    const parts = [quoteArg(lunaPath), ...args.map(quoteArg), quoteArg(filePath)];
    terminal.sendText(parts.join(" "));
    outputChannel.appendLine(`[${command}] ${parts.join(" ")}`);
}

function quoteArg(s: string): string {
    if (/^[A-Za-z0-9_\-.:/\\]+$/.test(s)) {
        return s;
    }
    return `"${s.replace(/"/g, '\\"')}"`;
}

function runCurrentFile(): Promise<void> {
    return runInTerminal("run", [], "Luna");
}

function buildCurrentFile(): Promise<void> {
    return runInTerminal("build", ["build"], "Luna build");
}

function checkCurrentFile(): Promise<void> {
    return runInTerminal("check", ["check"], "Luna check");
}

async function formatCurrentFile(): Promise<void> {
    if (!client) {
        vscode.window.showInformationMessage(
            "Luna formatting requires the language server (install the Luna toolchain).",
        );
        return;
    }
    await vscode.commands.executeCommand("editor.action.formatDocument");
}

export function deactivate(): Thenable<void> | undefined {
    if (client) {
        return client.stop();
    }
    return undefined;
}
