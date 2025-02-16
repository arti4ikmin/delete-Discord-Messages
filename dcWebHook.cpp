#include <fstream>
#include <sstream>
#include <iostream>
#include <curl/curl.h>
#include <cstdio>

static std::string escapeJ(const std::string &s) {
    std::string result;
    for (const char c : s) {
        switch (c) {
            case '\"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b";  break;
            case '\f': result += "\\f";  break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:
                // control characters, \uXXXX notation
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    result += buf;
                } else {
                    result.push_back(c);
                }
                break;
        }
    }
    return result;
}

void sendHookMsg(const std::string &message) {

    std::ifstream infile("preset_webhook.txt");
    if (!infile.good()) {
        return;
    }
    std::string url;
    std::getline(infile, url);
    const auto whitespace = " \t\n\r";
    const size_t start = url.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return;
    }
    const size_t end = url.find_last_not_of(whitespace);
    const std::string whUrl = url.substr(start, end - start + 1);


    
    if (whUrl.empty()) {
        return;
    }

    const std::string escapedMessage = escapeJ(message);
    const std::string jsonPayload = R"({"content":")" + escapedMessage + "\"}";

    CURL *curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL." << std::endl;
        return;
    }
    curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, whUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonPayload.c_str());

    if (const CURLcode res = curl_easy_perform(curl); res != CURLE_OK) {
        std::cerr << "CURL POST failed: " << curl_easy_strerror(res) << std::endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}