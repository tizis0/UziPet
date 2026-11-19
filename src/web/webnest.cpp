#include <winsock2.h>
#include <ws2tcpip.h>
#include "webnest.h"
#include "json.h"

static std::string jsonStr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; snprintf(b, 8, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

bool WebNest::start(const std::wstring& assetsDir, const std::wstring& facesDir) {
    if (port) return true;
    std::string html;
    if (!readFile(assetsDir + L"\\uzi_web.html", html)) {
        logf("web: no uzi_web.html");
        return false;
    }
    page = html;
    faces = facesDir;
    std::random_device rd;
    char tk[40];
    snprintf(tk, sizeof(tk), "%08x%08x%08x", rd(), rd(), rd());
    token = tk;
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd)) return false;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(kExtPort);
    if (bind(s, (sockaddr*)&a, sizeof(a))) {
        a.sin_port = 0;
        if (bind(s, (sockaddr*)&a, sizeof(a))) {
            closesocket(s);
            return false;
        }
        logf("web: port %d busy, browser extension will not connect", kExtPort);
    }
    if (listen(s, 8)) {
        closesocket(s);
        return false;
    }
    int len = sizeof(a);
    getsockname(s, (sockaddr*)&a, &len);
    port = ntohs(a.sin_port);
    sock = s;
    quit = false;
    th = std::thread([this] { loop(); });
    logf("web: listening on 127.0.0.1:%d", port);
    return true;
}

void WebNest::stop() {
    if (!port) return;
    if (testOnly) {
        port = 0;
        return;
    }
    quit = true;
    if (th.joinable()) th.join();
    closesocket((SOCKET)sock);
    WSACleanup();
    port = 0;
}

std::wstring WebNest::url() const { return L"http://127.0.0.1:" + std::to_wstring(port) + L"/" + widen(token) + L"/"; }

WebState WebNest::state() {
    std::lock_guard<std::mutex> g(mu);
    return st;
}

void WebNest::send(const std::string& json) {
    std::lock_guard<std::mutex> g(mu);
    cmds.push_back({++seq, json});
    if (cmds.size() > 40) cmds.erase(cmds.begin());
}

void WebNest::dropCmds() {
    std::lock_guard<std::mutex> g(mu);
    cmds.clear();
}

void WebNest::clearState() {
    std::lock_guard<std::mutex> g(mu);
    int v = st.ver;
    st = WebState();
    st.ver = v + 1;
}

void WebNest::testState(WebState s) {
    std::lock_guard<std::mutex> g(mu);
    s.any = true;
    s.ver = st.ver + 1;
    st = std::move(s);
    seen = nowSec();
}

std::vector<std::string> WebNest::testCmds(int& since) {
    std::lock_guard<std::mutex> g(mu);
    std::vector<std::string> out;
    for (auto& cm : cmds)
        if (cm.first > since) out.push_back(cm.second);
    since = seq;
    return out;
}

void WebNest::setName(const std::wstring& n) {
    std::lock_guard<std::mutex> g(mu);
    nameJs = jsonStr(narrow(n));
}

void WebNest::setShy(bool allowed) {
    std::lock_guard<std::mutex> g(mu);
    shyOk = allowed;
}

void WebNest::loop() {
    while (!quit) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET((SOCKET)sock, &rs);
        timeval tv{0, 200000};
        if (select(0, &rs, nullptr, nullptr, &tv) <= 0) continue;
        SOCKET c = accept((SOCKET)sock, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        DWORD to = 2000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
        setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof(to));
        handle(c);
        closesocket(c);
    }
}

static void reply(SOCKET c, int code, const char* type, const std::string& body) {
    char h[256];
    snprintf(h, sizeof(h),
             "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", code,
             code == 200 ? "OK" : "Nope", type, body.size());
    std::string out = std::string(h) + body;
    size_t off = 0;
    while (off < out.size()) {
        int n = ::send(c, out.data() + off, (int)std::min<size_t>(out.size() - off, 1 << 16), 0);
        if (n <= 0) break;
        off += n;
    }
}

void WebNest::handle(unsigned long long cs) {
    SOCKET c = (SOCKET)cs;
    std::string req;
    char buf[8192];
    size_t hdrEnd = std::string::npos;
    while (req.size() < (1 << 20)) {
        int n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, n);
        if (hdrEnd == std::string::npos) hdrEnd = req.find("\r\n\r\n");
        if (hdrEnd != std::string::npos) {
            size_t cl = 0;
            size_t p = req.find("Content-Length:");
            if (p == std::string::npos) p = req.find("content-length:");
            if (p != std::string::npos && p < hdrEnd) cl = (size_t)atoi(req.c_str() + p + 15);
            if (req.size() >= hdrEnd + 4 + cl) break;
        }
    }
    if (hdrEnd == std::string::npos) return;
    std::string host = "Host: 127.0.0.1:" + std::to_string(port);
    if (req.find(host) == std::string::npos && req.find("host: 127.0.0.1:" + std::to_string(port)) == std::string::npos) {
        reply(c, 403, "text/plain", "no");
        return;
    }
    size_t sp1 = req.find(' '), sp2 = req.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return;
    std::string method = req.substr(0, sp1), path = req.substr(sp1 + 1, sp2 - sp1 - 1);
    if (path.rfind("/ext/", 0) == 0) {
        std::string low = req.substr(0, hdrEnd);
        for (auto& ch : low) ch = (char)tolower((unsigned char)ch);
        size_t o = low.find("\r\nx-uzi: ");
        std::string key = o == std::string::npos ? "" : req.substr(o + 9, req.find("\r\n", o + 2) - o - 9);
        size_t og = low.find("\r\norigin: ");
        bool siteOrigin = og != std::string::npos && low.compare(og + 10, 19, "chrome-extension://") != 0;
        if (key != token || siteOrigin) {
            static int logged = 0;
            if (logged++ < 3) logf("web: /ext refused (key %s)", key.empty() ? "missing" : "wrong");
            reply(c, 403, "text/plain", "no");
            return;
        }
        extSeen = nowSec();
        path = path.substr(5);
    } else {
        std::string pre = "/" + token + "/";
        if (path.rfind(pre, 0) != 0) {
            reply(c, 404, "text/plain", "no");
            return;
        }
        path = path.substr(pre.size());
    }
    std::string query;
    size_t q = path.find('?');
    if (q != std::string::npos) {
        query = path.substr(q + 1);
        path = path.substr(0, q);
    }
    seen = nowSec();
    if (path.empty() || path == "index.html") {
        std::string h = page, nm, shy;
        {
            std::lock_guard<std::mutex> g(mu);
            nm = nameJs.empty() ? "\"\"" : nameJs;
            shy = shyOk ? "true" : "false";
        }
        auto put = [&](const char* k, const std::string& v) {
            for (size_t p; (p = h.find(k)) != std::string::npos;) h.replace(p, strlen(k), v);
        };
        put("__TOKEN__", token);
        put("__NAME__", nm);
        put("__SHY__", shy);
        reply(c, 200, "text/html; charset=utf-8", h);
        return;
    }
    if (path.rfind("face", 0) == 0 && path.size() == 9 && path.compare(5, 4, ".png") == 0 && path[4] >= '0' && path[4] <= '3') {
        std::string png;
        if (readFile(faces + L"\\" + widen(path), png)) reply(c, 200, "image/png", png);
        else reply(c, 404, "text/plain", "no");
        return;
    }
    if (path == "poll") {
        int since = 0;
        size_t sp = query.find("since=");
        if (sp != std::string::npos) since = atoi(query.c_str() + sp + 6);
        std::string out = "{\"seq\":";
        std::lock_guard<std::mutex> g(mu);
        out += std::to_string(seq) + ",\"cmds\":[";
        bool first = true;
        for (auto& cm : cmds)
            if (since >= 0 && cm.first > since) {
                if (!first) out += ",";
                out += cm.second;
                first = false;
            }
        out += "]}";
        reply(c, 200, "application/json", out);
        return;
    }
    if (path == "state" && method == "POST") {
        std::string body = req.substr(hdrEnd + 4);
        bool ok = false;
        Json j = Json::parse(body, &ok);
        if (ok) {
            WebState n;
            n.any = true;
            n.visible = j["visible"].flag();
            n.focused = j["focused"].flag();
            n.closed = j["closed"].flag();
            n.iw = j["iw"].f();
            n.ih = j["ih"].f();
            n.dpr = j["dpr"].f(1);
            n.sy = j["sy"].f();
            n.sh = j["sh"].f();
            n.cw = j["cw"].f();
            n.mode = j["mode"].str();
            n.shame = j["shame"].i();
            n.topic = widen(j["topic"].str());
            n.title = widen(j["title"].str());
            const Json& els = j["els"];
            for (size_t i = 0; i < els.size(); i++) {
                const Json& e = els[i];
                WebEl w;
                w.id = e[0].i();
                w.x = e[1].f();
                w.y = e[2].f();
                w.w = e[3].f();
                w.h = e[4].f();
                w.type = e[5].i();
                n.els.push_back(w);
            }
            std::lock_guard<std::mutex> g(mu);
            if (n.mode != st.mode || n.visible != st.visible || n.closed)
                logf("web: page mode=%s shame=%d visible=%d focused=%d closed=%d %.0fx%.0f dpr %.2f, %zu blocks", n.mode.c_str(), n.shame,
                     n.visible, n.focused, n.closed, n.iw, n.ih, n.dpr, n.els.size());
            n.ver = st.ver + 1;
            st = std::move(n);
        }
        reply(c, 200, "application/json", "{}");
        return;
    }
    reply(c, 404, "text/plain", "no");
}
