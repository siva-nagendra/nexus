/**
 * ClaudeShell Terminal v2 — Frontend Logic
 *
 * WebSocket-based terminal with search, links, themes, toolbar, status bar.
 * Falls back to HTTP polling if WebSocket is unavailable.
 */
(function () {
    'use strict';

    // ── Configuration ────────────────────────────────────────────
    var RECONNECT_BASE = 1000;
    var RECONNECT_MAX = 10000;
    var POLL_INTERVAL = 50;

    // Determine server URL from page origin or meta tag
    var PORT = parseInt(window.location.port) || 19220;
    var HTTP_BASE = window.location.protocol === 'http:'
        ? window.location.origin
        : 'http://127.0.0.1:' + PORT;
    var WS_URL = 'ws://127.0.0.1:' + PORT;

    // ── State ────────────────────────────────────────────────────
    var term, fitAddon, searchAddon, webLinksAddon, webglAddon;
    var ws = null;
    var wsConnected = false;
    var reconnectDelay = RECONNECT_BASE;
    var reconnectTimer = null;
    var pollingFallback = false;
    var fontSize = parseInt(localStorage.getItem('cs-fontsize') || '14');
    var currentTheme = localStorage.getItem('cs-theme') || 'mocha';

    // ── Theme Definitions ────────────────────────────────────────
    var THEMES = {
        mocha: {
            background: '#1e1e2e', foreground: '#cdd6f4',
            cursor: '#f5e0dc', cursorAccent: '#1e1e2e',
            selectionBackground: '#585b70',
            black: '#45475a', red: '#f38ba8', green: '#a6e3a1',
            yellow: '#f9e2af', blue: '#89b4fa', magenta: '#f5c2e7',
            cyan: '#94e2d5', white: '#bac2de',
            brightBlack: '#585b70', brightRed: '#f38ba8', brightGreen: '#a6e3a1',
            brightYellow: '#f9e2af', brightBlue: '#89b4fa', brightMagenta: '#f5c2e7',
            brightCyan: '#94e2d5', brightWhite: '#a6adc8'
        },
        latte: {
            background: '#eff1f5', foreground: '#4c4f69',
            cursor: '#dc8a78', cursorAccent: '#eff1f5',
            selectionBackground: '#acb0be',
            black: '#5c5f77', red: '#d20f39', green: '#40a02b',
            yellow: '#df8e1d', blue: '#1e66f5', magenta: '#ea76cb',
            cyan: '#179299', white: '#acb0be',
            brightBlack: '#6c6f85', brightRed: '#d20f39', brightGreen: '#40a02b',
            brightYellow: '#df8e1d', brightBlue: '#1e66f5', brightMagenta: '#ea76cb',
            brightCyan: '#179299', brightWhite: '#bcc0cc'
        },
        dracula: {
            background: '#282a36', foreground: '#f8f8f2',
            cursor: '#f8f8f2', cursorAccent: '#282a36',
            selectionBackground: '#44475a',
            black: '#21222c', red: '#ff5555', green: '#50fa7b',
            yellow: '#f1fa8c', blue: '#bd93f9', magenta: '#ff79c6',
            cyan: '#8be9fd', white: '#f8f8f2',
            brightBlack: '#6272a4', brightRed: '#ff6e6e', brightGreen: '#69ff94',
            brightYellow: '#ffffa5', brightBlue: '#d6acff', brightMagenta: '#ff92df',
            brightCyan: '#a4ffff', brightWhite: '#ffffff'
        },
        onedark: {
            background: '#282c34', foreground: '#abb2bf',
            cursor: '#528bff', cursorAccent: '#282c34',
            selectionBackground: '#3e4451',
            black: '#3f4451', red: '#e06c75', green: '#98c379',
            yellow: '#e5c07b', blue: '#61afef', magenta: '#c678dd',
            cyan: '#56b6c2', white: '#abb2bf',
            brightBlack: '#5c6370', brightRed: '#e06c75', brightGreen: '#98c379',
            brightYellow: '#e5c07b', brightBlue: '#61afef', brightMagenta: '#c678dd',
            brightCyan: '#56b6c2', brightWhite: '#ffffff'
        }
    };

    // ── Initialize xterm.js ──────────────────────────────────────
    function initTerminal() {
        term = new Terminal({
            cursorBlink: true,
            cursorStyle: 'bar',
            fontSize: fontSize,
            fontFamily: '"Cascadia Code", "Fira Code", "JetBrains Mono", "Consolas", monospace',
            theme: THEMES[currentTheme],
            allowProposedApi: true,
            scrollback: 50000,
            rightClickSelectsWord: true,
            drawBoldTextInBrightColors: true,
            convertEol: false,
        });

        // Addons
        fitAddon = new FitAddon.FitAddon();
        term.loadAddon(fitAddon);

        // Search addon
        if (typeof SearchAddon !== 'undefined') {
            searchAddon = new SearchAddon.SearchAddon();
            term.loadAddon(searchAddon);
        }

        // Web links addon
        if (typeof WebLinksAddon !== 'undefined') {
            webLinksAddon = new WebLinksAddon.WebLinksAddon(function (event, uri) {
                window.open(uri, '_blank');
            });
            term.loadAddon(webLinksAddon);
        }

        // Open terminal in the DOM
        term.open(document.getElementById('terminal'));

        // WebGL addon (load after open)
        if (typeof WebglAddon !== 'undefined') {
            try {
                webglAddon = new WebglAddon.WebglAddon();
                webglAddon.onContextLost(function () {
                    webglAddon.dispose();
                    webglAddon = null;
                });
                term.loadAddon(webglAddon);
            } catch (e) {
                console.warn('WebGL addon failed, using canvas renderer:', e);
            }
        }

        fitAddon.fit();

        // Auto-resize on container changes
        var resizeObs = new ResizeObserver(function () {
            fitAddon.fit();
            sendResize();
        });
        resizeObs.observe(document.getElementById('terminal'));

        // Input handler
        term.onData(function (data) {
            sendInput(data);
        });

        // Keyboard shortcuts
        term.attachCustomKeyEventHandler(handleKeyEvent);
    }

    // ── Keyboard Handler ─────────────────────────────────────────
    function handleKeyEvent(ev) {
        if (ev.type !== 'keydown') return true;

        // Ctrl+F → toggle search
        if (ev.ctrlKey && !ev.shiftKey && ev.key === 'f') {
            toggleSearch();
            return false;
        }

        // Ctrl+L → clear terminal
        if (ev.ctrlKey && !ev.shiftKey && ev.key === 'l') {
            term.clear();
            return false;
        }

        // Ctrl+Shift+C → copy
        if (ev.ctrlKey && ev.shiftKey && ev.key === 'C') {
            copySelection();
            return false;
        }

        // Ctrl+Shift+V → paste
        if (ev.ctrlKey && ev.shiftKey && ev.key === 'V') {
            pasteClipboard();
            return false;
        }

        // Ctrl+C → copy if selection, else send interrupt
        if (ev.ctrlKey && !ev.shiftKey && ev.key === 'c') {
            if (term.hasSelection()) {
                copySelection();
                return false;
            }
            // Fall through — xterm sends \x03
        }

        // Ctrl+V → paste
        if (ev.ctrlKey && !ev.shiftKey && ev.key === 'v') {
            pasteClipboard();
            return false;
        }

        // Ctrl+= / Ctrl+- → font size
        if (ev.ctrlKey && (ev.key === '=' || ev.key === '+')) {
            changeFontSize(1);
            return false;
        }
        if (ev.ctrlKey && ev.key === '-') {
            changeFontSize(-1);
            return false;
        }

        // Ctrl+0 → reset font size
        if (ev.ctrlKey && ev.key === '0') {
            setFontSize(14);
            return false;
        }

        return true;
    }

    // ── Clipboard ────────────────────────────────────────────────
    function copySelection() {
        var sel = term.getSelection();
        if (sel) {
            navigator.clipboard.writeText(sel).catch(function () {});
            term.clearSelection();
        }
    }

    function pasteClipboard() {
        navigator.clipboard.readText().then(function (text) {
            if (text) sendInput(text);
        }).catch(function () {});
    }

    // ── Font Size ────────────────────────────────────────────────
    function changeFontSize(delta) {
        setFontSize(fontSize + delta);
    }

    function setFontSize(size) {
        fontSize = Math.max(8, Math.min(32, size));
        term.options.fontSize = fontSize;
        fitAddon.fit();
        sendResize();
        localStorage.setItem('cs-fontsize', String(fontSize));
        var el = document.getElementById('font-size-value');
        if (el) el.textContent = fontSize + 'px';
        var slider = document.getElementById('font-size-slider');
        if (slider) slider.value = fontSize;
    }

    // ── Theme ────────────────────────────────────────────────────
    function setTheme(name) {
        if (!THEMES[name]) return;
        currentTheme = name;
        term.options.theme = THEMES[name];
        document.documentElement.setAttribute('data-theme', name);
        localStorage.setItem('cs-theme', name);

        // Update active state on theme buttons
        document.querySelectorAll('.theme-btn').forEach(function (btn) {
            btn.classList.toggle('active', btn.dataset.theme === name);
        });
    }

    // ── WebSocket Connection ─────────────────────────────────────
    function connectWebSocket() {
        if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
            return;
        }

        ws = new WebSocket(WS_URL);

        ws.onopen = function () {
            wsConnected = true;
            reconnectDelay = RECONNECT_BASE;
            pollingFallback = false;
            setConnectionStatus('connected');
            console.log('[ClaudeShell] WebSocket connected');
        };

        ws.onmessage = function (ev) {
            try {
                var msg = JSON.parse(ev.data);
                handleServerMessage(msg);
            } catch (e) {
                console.error('[ClaudeShell] Bad message:', e);
            }
        };

        ws.onclose = function () {
            wsConnected = false;
            ws = null;
            setConnectionStatus('reconnecting');
            console.log('[ClaudeShell] WebSocket closed, reconnecting in', reconnectDelay, 'ms');
            reconnectTimer = setTimeout(function () {
                reconnectDelay = Math.min(reconnectDelay * 1.5, RECONNECT_MAX);
                connectWebSocket();
            }, reconnectDelay);
        };

        ws.onerror = function () {
            // onclose will fire after this
        };
    }

    function handleServerMessage(msg) {
        switch (msg.type) {
            case 'output':
                if (msg.data) writeBase64(msg.data);
                break;
            case 'scrollback':
                if (msg.data) writeBase64(msg.data);
                break;
            case 'exit':
                term.write('\r\n\x1b[33m[Process exited with code ' + (msg.code || 0) + '. Press Enter to restart]\x1b[0m\r\n');
                waitForRestartKey();
                break;
            case 'status':
                updateStatusInfo(msg);
                break;
            case 'pong':
                break;
        }
    }

    function writeBase64(b64) {
        var binary = atob(b64);
        var bytes = new Uint8Array(binary.length);
        for (var i = 0; i < binary.length; i++) {
            bytes[i] = binary.charCodeAt(i);
        }
        term.write(new TextDecoder('utf-8').decode(bytes));
    }

    function waitForRestartKey() {
        var disposable = term.onKey(function (ev) {
            if (ev.key === '\r') {
                disposable.dispose();
                term.write('\r\n\x1b[36m[Restarting...]\x1b[0m\r\n');
                sendMessage({ type: 'restart' });
            }
        });
    }

    // ── Send Messages ────────────────────────────────────────────
    function sendMessage(obj) {
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify(obj));
        }
    }

    function sendInput(data) {
        sendMessage({ type: 'input', data: data });
    }

    function sendResize() {
        if (term) {
            sendMessage({ type: 'resize', rows: term.rows, cols: term.cols });
            var el = document.getElementById('status-dims');
            if (el) el.textContent = term.cols + '\u00d7' + term.rows;
        }
    }

    // ── Search ───────────────────────────────────────────────────
    var searchVisible = false;
    var searchRegex = false;
    var searchCase = false;

    function toggleSearch() {
        searchVisible = !searchVisible;
        var overlay = document.getElementById('search-overlay');
        overlay.classList.toggle('visible', searchVisible);
        if (searchVisible) {
            document.getElementById('search-input').focus();
        } else {
            if (searchAddon) searchAddon.clearDecorations();
            term.focus();
        }
    }

    function doSearch(direction) {
        if (!searchAddon) return;
        var query = document.getElementById('search-input').value;
        if (!query) return;

        var opts = {
            regex: searchRegex,
            caseSensitive: searchCase,
            decorations: {
                matchBackground: '#f9e2af44',
                matchBorder: '#f9e2af',
                matchOverviewRuler: '#f9e2af',
                activeMatchBackground: '#f38ba877',
                activeMatchBorder: '#f38ba8',
                activeMatchColorOverviewRuler: '#f38ba8',
            }
        };

        if (direction === 'prev') {
            searchAddon.findPrevious(query, opts);
        } else {
            searchAddon.findNext(query, opts);
        }
    }

    // ── Status Bar ───────────────────────────────────────────────
    function setConnectionStatus(status) {
        var dot = document.getElementById('status-dot');
        var label = document.getElementById('status-label');
        if (!dot || !label) return;

        dot.className = 'status-dot ' + status;
        var labels = { connected: 'Connected', reconnecting: 'Reconnecting...', disconnected: 'Disconnected' };
        label.textContent = labels[status] || status;
    }

    function updateStatusInfo(msg) {
        if (msg.cwd) {
            var parts = msg.cwd.replace(/\\/g, '/').split('/').filter(Boolean);
            var name = parts[parts.length - 1] || 'Unknown';
            var el = document.getElementById('status-project');
            if (el) el.textContent = name;
        }
        var portEl = document.getElementById('status-port');
        if (portEl) portEl.textContent = ':' + PORT;
    }

    // ── Context Menu ─────────────────────────────────────────────
    function showContextMenu(x, y) {
        var menu = document.getElementById('context-menu');
        menu.style.left = x + 'px';
        menu.style.top = y + 'px';
        menu.classList.add('visible');
    }

    function hideContextMenu() {
        document.getElementById('context-menu').classList.remove('visible');
    }

    function handleContextAction(action) {
        hideContextMenu();
        switch (action) {
            case 'copy': copySelection(); break;
            case 'paste': pasteClipboard(); break;
            case 'selectall': term.selectAll(); break;
            case 'search': toggleSearch(); break;
            case 'clear': term.clear(); break;
            case 'browser': window.open(HTTP_BASE, '_blank'); break;
            case 'restart': sendMessage({ type: 'restart' }); break;
        }
    }

    // ── Settings Panel ───────────────────────────────────────────
    var settingsVisible = false;

    function toggleSettings() {
        settingsVisible = !settingsVisible;
        var panel = document.getElementById('settings-panel');
        if (settingsVisible) {
            var btn = document.getElementById('btn-settings');
            var rect = btn.getBoundingClientRect();
            panel.style.right = (window.innerWidth - rect.right) + 'px';
            panel.style.top = rect.bottom + 4 + 'px';
        }
        panel.classList.toggle('visible', settingsVisible);
    }

    // ── Wire up DOM ──────────────────────────────────────────────
    function bindUI() {
        // Toolbar buttons
        document.getElementById('btn-restart').onclick = function () {
            sendMessage({ type: 'restart' });
        };
        document.getElementById('btn-clear').onclick = function () {
            term.clear();
        };
        document.getElementById('btn-search').onclick = toggleSearch;
        document.getElementById('btn-browser').onclick = function () {
            window.open(HTTP_BASE, '_blank');
        };
        document.getElementById('btn-settings').onclick = toggleSettings;

        // Search overlay
        document.getElementById('search-input').addEventListener('keydown', function (ev) {
            if (ev.key === 'Enter') {
                doSearch(ev.shiftKey ? 'prev' : 'next');
                ev.preventDefault();
            }
            if (ev.key === 'Escape') {
                toggleSearch();
                ev.preventDefault();
            }
        });
        document.getElementById('search-input').addEventListener('input', function () {
            doSearch('next');
        });
        document.getElementById('search-regex').onclick = function () {
            searchRegex = !searchRegex;
            this.classList.toggle('active', searchRegex);
            doSearch('next');
        };
        document.getElementById('search-case').onclick = function () {
            searchCase = !searchCase;
            this.classList.toggle('active', searchCase);
            doSearch('next');
        };
        document.getElementById('search-prev').onclick = function () { doSearch('prev'); };
        document.getElementById('search-next').onclick = function () { doSearch('next'); };
        document.getElementById('search-close').onclick = toggleSearch;

        // Context menu
        document.getElementById('terminal').addEventListener('contextmenu', function (ev) {
            ev.preventDefault();
            showContextMenu(ev.clientX, ev.clientY);
        });
        document.addEventListener('click', function (ev) {
            if (!ev.target.closest('#context-menu')) hideContextMenu();
            if (!ev.target.closest('#settings-panel') && !ev.target.closest('#btn-settings')) {
                settingsVisible = false;
                document.getElementById('settings-panel').classList.remove('visible');
            }
        });
        document.querySelectorAll('.ctx-item').forEach(function (el) {
            el.addEventListener('click', function () {
                handleContextAction(this.dataset.action);
            });
        });

        // Settings: font size
        var slider = document.getElementById('font-size-slider');
        if (slider) {
            slider.value = fontSize;
            document.getElementById('font-size-value').textContent = fontSize + 'px';
            slider.addEventListener('input', function () {
                setFontSize(parseInt(this.value));
            });
        }

        // Settings: theme
        document.querySelectorAll('.theme-btn').forEach(function (btn) {
            btn.addEventListener('click', function () {
                setTheme(this.dataset.theme);
            });
        });

        // Global escape to close overlays
        document.addEventListener('keydown', function (ev) {
            if (ev.key === 'Escape') {
                hideContextMenu();
                if (settingsVisible) toggleSettings();
            }
        });
    }

    // ── Backward-compatible globals (for C++ bridge calls) ───────
    window.claudeShellClear = function () { if (term) term.clear(); };
    window.claudeShellReceive = function (b64) { if (term) writeBase64(b64); };
    window.claudeShellProcessExit = function (code) {
        if (term) {
            term.write('\r\n\x1b[33m[Process exited with code ' + code + ']\x1b[0m\r\n');
        }
    };

    // ── Boot ─────────────────────────────────────────────────────
    function boot() {
        // Apply saved theme
        document.documentElement.setAttribute('data-theme', currentTheme);

        initTerminal();
        bindUI();
        setTheme(currentTheme);
        setConnectionStatus('reconnecting');

        // Connect
        connectWebSocket();
    }

    // Start when DOM is ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', boot);
    } else {
        boot();
    }
})();
