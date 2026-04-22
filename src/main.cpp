#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <stdexcept>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ─── HTTPS GET (POSIX + OpenSSL) ─────────────────────────────────────────────

static std::string https_get(const std::string& host, const std::string& path) {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), "443", &hints, &res) != 0)
        throw std::runtime_error("getaddrinfo failed for " + host);

    int sock = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) { freeaddrinfo(res); throw std::runtime_error("socket() failed"); }
    if (::connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        ::close(sock); freeaddrinfo(res);
        throw std::runtime_error("connect() to " + host + " failed");
    }
    freeaddrinfo(res);

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { ::close(sock); throw std::runtime_error("SSL_CTX_new failed"); }
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, host.c_str()); // SNI
    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl); SSL_CTX_free(ctx); ::close(sock);
        throw std::runtime_error("SSL_connect failed");
    }

    std::string req = "GET " + path + " HTTP/1.0\r\n"
                      "Host: " + host + "\r\n"
                      "User-Agent: CppWeather/1.0\r\n"
                      "Accept: application/json\r\n\r\n";
    SSL_write(ssl, req.c_str(), (int)req.size());

    std::string raw;
    char buf[8192];
    int n;
    while ((n = SSL_read(ssl, buf, sizeof(buf))) > 0) raw.append(buf, n);

    SSL_free(ssl); SSL_CTX_free(ctx); ::close(sock);

    // Strip HTTP headers
    auto pos = raw.find("\r\n\r\n");
    if (pos == std::string::npos) throw std::runtime_error("Malformed HTTP response");
    return raw.substr(pos + 4);
}

// ─── ANSI Colors ──────────────────────────────────────────────────────────────

static const std::string
    RST  = "\033[0m",
    BOLD = "\033[1m",
    DIM  = "\033[2m",
    BLU  = "\033[34m",
    CYN  = "\033[36m",
    BBLU = "\033[94m",
    BCYN = "\033[96m",
    BGRN = "\033[92m",
    BYLW = "\033[93m",
    BRED = "\033[91m",
    BWHT = "\033[97m";

// ─── ASCII Weather Icons ──────────────────────────────────────────────────────
// 5 rows of exactly 12 printable ASCII characters each.

struct Icon { std::vector<std::string> rows; std::string color; };

static Icon make_icon(const std::string& desc) {
    std::string d = desc;
    for (auto& ch : d) ch = (char)tolower((unsigned char)ch);

    if (d.find("thunder") != std::string::npos)
        return {{"    .--.    ",
                 " .-(    ).  ",
                 " (___.__) ) ",
                 "   /\\ /\\   ",
                 "  /  V  \\  "}, BYLW};

    if (d.find("blizzard") != std::string::npos || d.find("snow") != std::string::npos)
        return {{"    .--.    ",
                 " .-(    ).  ",
                 " (___.__) ) ",
                 "  *  *  *   ",
                 " *  *  *    "}, BBLU};

    if (d.find("sleet") != std::string::npos || d.find("freezing") != std::string::npos)
        return {{"    .--.    ",
                 " .-(    ).  ",
                 " (___.__) ) ",
                 "  '* '* '*  ",
                 " *' *' *'   "}, BBLU};

    if (d.find("rain")    != std::string::npos ||
        d.find("drizzle") != std::string::npos ||
        d.find("shower")  != std::string::npos)
        return {{"    .--.    ",
                 " .-(    ).  ",
                 " (___.__) ) ",
                 "  ' ' ' '   ",
                 "   ' ' '    "}, BBLU};

    if (d.find("fog")  != std::string::npos ||
        d.find("mist") != std::string::npos ||
        d.find("haze") != std::string::npos)
        return {{"            ",
                 " _ - _ - _  ",
                 "  - _ - _ - ",
                 " _ - _ - _  ",
                 "            "}, DIM};

    if (d.find("overcast") != std::string::npos)
        return {{"            ",
                 "    .--.    ",
                 " .-(    ).  ",
                 " (___.__) ) ",
                 "            "}, DIM + BWHT};

    if (d.find("cloud") != std::string::npos || d.find("partly") != std::string::npos)
        return {{"   \\  /     ",
                 " _/\"\".-.   ",
                 "   \\_(  ).  ",
                 "   (___.__) ",
                 "            "}, BYLW};

    // Clear / Sunny
    return {{"   \\  /     ",
             "    .-.     ",
             "- (   ) -   ",
             "    `-'     ",
             "   /  \\    "}, BYLW};
}

// ─── Formatting Helpers ───────────────────────────────────────────────────────

static std::string temp_color(int c) {
    if (c <= 0)  return BBLU;
    if (c <= 10) return BLU;
    if (c <= 18) return BCYN;
    if (c <= 25) return BGRN;
    if (c <= 32) return BYLW;
    return BRED;
}

static std::string bar(int value, int maxv, int width) {
    int n = maxv > 0 ? std::clamp(value * width / maxv, 0, width) : 0;
    return BCYN + std::string(n, '#') + DIM + std::string(width - n, '.') + RST;
}

// Right-pad a plain-ASCII string to w characters.
static std::string rpad(std::string s, int w) {
    while ((int)s.size() < w) s += ' ';
    return s;
}

// "2026-04-22" -> "Wed 22 Apr"  (Tomohiko Sakamoto algorithm)
static std::string fmt_date(const std::string& iso) {
    if ((int)iso.size() < 10) return iso;
    static const char* mon[] = {"","Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};
    static const char* dow[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const int   t[]   = {0,3,2,5,0,3,5,1,4,6,2,4};
    int y = std::stoi(iso.substr(0, 4));
    int m = std::stoi(iso.substr(5, 2));
    int d = std::stoi(iso.substr(8, 2));
    int yy = y - (m < 3 ? 1 : 0);
    int dw = (yy + yy/4 - yy/100 + yy/400 + t[m-1] + d) % 7;
    std::ostringstream ss;
    ss << dow[dw] << " " << std::setw(2) << std::setfill('0') << d << " " << mon[m];
    return ss.str();
}

// ─── Display ──────────────────────────────────────────────────────────────────

static void print_weather(const json& data) {
    const auto& cur  = data["current_condition"][0];
    const auto& area = data["nearest_area"][0];

    int temp_c   = std::stoi(cur["temp_C"].get<std::string>());
    int feels_c  = std::stoi(cur["FeelsLikeC"].get<std::string>());
    int humidity = std::stoi(cur["humidity"].get<std::string>());
    int wind_kph = std::stoi(cur["windspeedKmph"].get<std::string>());
    int vis_km   = std::stoi(cur["visibility"].get<std::string>());
    int pressure = std::stoi(cur["pressure"].get<std::string>());
    int uv       = std::stoi(cur["uvIndex"].get<std::string>());
    std::string wind_dir = cur["winddir16Point"].get<std::string>();
    std::string desc     = cur["weatherDesc"][0]["value"].get<std::string>();
    std::string aname    = area["areaName"][0]["value"].get<std::string>();
    std::string country  = area["country"][0]["value"].get<std::string>();
    std::string obs_time = cur["observation_time"].get<std::string>();

    Icon icon = make_icon(desc);

    // ── Header box ────────────────────────────────────────────────────────────
    const int W = 62;
    std::string title = " WEATHER DASHBOARD \xe2\x80\x94 " + aname + ", " + country + " ";
    // \xe2\x80\x94 = UTF-8 em dash (—)
    int lp = ((W - 2) - (int)title.size()) / 2;
    int rp = (W - 2) - lp - (int)title.size();
    if (lp < 0) { lp = 0; rp = 0; }

    auto hline = [&](const char* l, const char* r) {
        std::cout << l;
        for (int i = 0; i < W - 2; ++i) std::cout << "\xe2\x95\x90"; // ═
        std::cout << r << "\n";
    };

    std::cout << "\n" << BCYN << BOLD;
    hline("\xe2\x95\x94", "\xe2\x95\x97"); // ╔...╗
    std::cout << "\xe2\x95\x91"             // ║
              << std::string(lp, ' ') << title << std::string(rp, ' ')
              << "\xe2\x95\x91\n";
    hline("\xe2\x95\x9a", "\xe2\x95\x9d"); // ╚...╝
    std::cout << RST;

    // ── Current conditions ────────────────────────────────────────────────────
    std::cout << "\n" << BOLD << BCYN << " Current Conditions" << RST
              << DIM << "  (observed " << obs_time << ")\n\n" << RST;

    auto fmt_temp = [&](int t_) {
        return temp_color(t_) + BOLD + std::to_string(t_) + "\xc2\xb0" + "C" + RST;
        // \xc2\xb0 = UTF-8 degree sign (°)
    };

    std::vector<std::string> info = {
        BCYN + "  Temperature " + RST + ": " + fmt_temp(temp_c)
             + DIM + "  feels like " + RST + fmt_temp(feels_c),
        BCYN + "  Humidity    " + RST + ": " + bar(humidity, 100, 16)
             + " " + BOLD + std::to_string(humidity) + "%" + RST,
        BCYN + "  Wind        " + RST + ": " + BOLD
             + std::to_string(wind_kph) + " km/h " + wind_dir + RST,
        BCYN + "  Visibility  " + RST + ": " + BOLD + std::to_string(vis_km) + " km" + RST,
        BCYN + "  Pressure    " + RST + ": " + BOLD + std::to_string(pressure) + " hPa" + RST,
        BCYN + "  UV Index    " + RST + ": " + BOLD + std::to_string(uv) + RST,
        BCYN + "  Conditions  " + RST + ": " + BYLW + BOLD + desc + RST,
    };

    int rows = (int)std::max(icon.rows.size(), info.size());
    for (int i = 0; i < rows; ++i) {
        std::string icon_part = (i < (int)icon.rows.size())
            ? icon.color + icon.rows[i] + RST
            : std::string(12, ' ');
        std::string info_part = (i < (int)info.size()) ? info[i] : "";
        std::cout << "  " << icon_part << "   " << info_part << "\n";
    }

    // ── 3-Day Forecast ────────────────────────────────────────────────────────
    std::cout << "\n" << CYN;
    const std::string div_label = " 3-Day Forecast ";
    int div_side = (W - (int)div_label.size()) / 2;
    for (int i = 0; i < div_side; ++i) std::cout << "\xe2\x94\x80"; // ─
    std::cout << BOLD << div_label << RST << CYN;
    for (int i = 0; i < div_side; ++i) std::cout << "\xe2\x94\x80";
    std::cout << RST << "\n\n";

    const auto& fc = data["weather"];
    int ndays = std::min((int)fc.size(), 3);

    struct Day { std::string date, desc; int hi, lo; };
    std::vector<Day> days;
    for (int i = 0; i < ndays; ++i) {
        const auto& fw = fc[i];
        std::string dc = "Clear";
        // hourly[4] is the noon slot (0=0:00, 1=3:00, ..., 4=12:00)
        if (fw.contains("hourly") && fw["hourly"].size() > 4)
            dc = fw["hourly"][4]["weatherDesc"][0]["value"].get<std::string>();
        days.push_back({
            fw["date"].get<std::string>(), dc,
            std::stoi(fw["maxtempC"].get<std::string>()),
            std::stoi(fw["mintempC"].get<std::string>())
        });
    }

    std::vector<Icon> ficons;
    for (auto& day : days) ficons.push_back(make_icon(day.desc));

    const int CW = 20; // card width in printable ASCII chars

    // Date headers
    for (int i = 0; i < ndays; ++i)
        std::cout << "  " << BOLD << BCYN << rpad(fmt_date(days[i].date), CW) << RST;
    std::cout << "\n";

    // Separators
    for (int i = 0; i < ndays; ++i) {
        std::cout << "  " << CYN;
        for (int j = 0; j < CW; ++j) std::cout << "\xe2\x94\x80"; // ─
        std::cout << RST;
    }
    std::cout << "\n";

    // Icon rows (5 rows, 12 ASCII chars each)
    for (int row = 0; row < 5; ++row) {
        for (int i = 0; i < ndays; ++i) {
            std::string r = (row < (int)ficons[i].rows.size())
                ? ficons[i].rows[row] : "            ";
            std::cout << "  " << ficons[i].color << rpad(r, CW) << RST;
        }
        std::cout << "\n";
    }

    // Condition descriptions
    for (int i = 0; i < ndays; ++i)
        std::cout << "  " << BYLW << rpad(days[i].desc, CW) << RST;
    std::cout << "\n";

    // Hi / Lo (plain ASCII so rpad alignment is reliable)
    for (int i = 0; i < ndays; ++i) {
        std::string hi_s = "Hi:" + std::to_string(days[i].hi) + "C";
        std::string lo_s = "Lo:" + std::to_string(days[i].lo) + "C";
        std::string full = hi_s + "  " + lo_s;
        std::cout << "  "
                  << temp_color(days[i].hi) << BOLD << hi_s << RST
                  << "  "
                  << temp_color(days[i].lo)        << lo_s << RST
                  << std::string(CW - (int)full.size(), ' ');
    }
    std::cout << "\n\n";
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        std::cout << "Usage: " << argv[0] << " [city]\n"
                  << "  Displays an ASCII weather dashboard for the given city.\n"
                  << "  City defaults to London. Multi-word cities: quote them.\n";
        return 0;
    }

    std::string city = "London";
    if (argc >= 2) {
        city = argv[1];
        for (int i = 2; i < argc; ++i) city += "+" + std::string(argv[i]);
    }
    std::replace(city.begin(), city.end(), ' ', '+');

    try {
        std::cerr << DIM << "Fetching weather for " << city << "..." << RST << "\n";
        std::string raw = https_get("wttr.in", "/" + city + "?format=j1");

        if (raw.empty() || raw[0] != '{')
            throw std::runtime_error("Unexpected response — check the city name.");

        json data = json::parse(raw);
        print_weather(data);
    } catch (const std::exception& e) {
        std::cerr << BRED << "Error: " << e.what() << RST << "\n";
        return 1;
    }

    return 0;
}
