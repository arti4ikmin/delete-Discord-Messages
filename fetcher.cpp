#include "fetcher.h"
#include <iostream>
#include <string>
#include <thread>
#include <random>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include "main.h"

using json = nlohmann::json;

struct MemoryStruct {
    char* memory;
    size_t size;
};

static size_t WriteCallback(const void* contents, const size_t size, const size_t nmemb, void* userp) {
    const size_t realsize = size * nmemb;
    auto* mem = static_cast<struct MemoryStruct*>(userp);

    const auto ptr = static_cast<char*>(realloc(mem->memory, mem->size + realsize + 1));
    if (!ptr) return 0;

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

std::pair<long, std::string> dcReq(const std::string& url, const std::string& method) {
    MemoryStruct chunk{};
    chunk.memory = static_cast<char*>(malloc(1));
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* curl = curl_easy_init();
    long http_code = 0;

    if (curl) {
        curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("Authorization: " + TOKEN).c_str());
        headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/121.0.0.0 Safari/537.36");
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(&chunk));

        if (const CURLcode res = curl_easy_perform(curl); res != CURLE_OK) {
            std::cout << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        } else {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }

    std::string response(chunk.memory, chunk.size);
    free(chunk.memory);
    curl_global_cleanup();

    return { http_code, response };
}

json fetchMsgsGuild(const std::string& query) {
    int fuckupcounter = 0;
    float delayBetweenFetch = 2.5f;
    json result = json::array();
    int offset = 0;
    int page = 0;

    while (true) {

        // compiler tells "String concatenation results in allocation of unnecessary temporary strings; consider using 'operator+='", so Ive decided to just make as it says lol
        std::string url = "https://discord.com/api/v9/guilds/";
        url += TARGET_ID;
        url += "/messages/search?";
        url += query;
        url += "&offset=";
        url += std::to_string(offset);

        
        if (verbose_fetch) {
            std::cout << "Fetching guild page " << page + 1 << " (" << offset << " offset)" << std::endl;
        }
        
        auto [status, response] = dcReq(url);
        
        if (status == 400) {
            std::cout << "Reached end of results (this should appear when results reached 10k offset, discord cant handle more) (400 Bad Request)" << std::endl;
            break;
        }
        if (status != 200 && status != 429) {
            std::cout << "Err fetching messages: HTTP " << status << std::endl;
            break;
        }
        
        try {
            json data = json::parse(response);
            const int results = static_cast<int>(data["messages"].size());
            
            if (verbose_fetch) {
                std::cout << "Page " << page + 1 << ": Found " << results << " more messages." << std::endl;
            }

            for (auto& group : data["messages"]) {
                auto& msg = group[0];
                json entry = {
                    {"id", msg["id"].get<std::string>()},
                    {"channel_id", msg["channel_id"].get<std::string>()},
                    {"deleted", false},
                    {"last_attempt", 0}
                };
                result.push_back(entry);
            }
            saveProgress(result);
            
            if (results == 0) {
                if (status == 429)
                {
                    std::cout << "\nOh no! It seems like you are rate limited, lets wait a few seconds to cool down.\t (Response was: " << response << ")" << std::endl;
                    randDelay(8.0f, 1.0f);
                    if (delayBetweenFetch < 6.0f)
                    {
                        delayBetweenFetch += 0.5f;
                    }
                    fuckupcounter++;
                    if (fuckupcounter > 15 || offset > 9900) { break; }
                    // break; // COMMENT OUT IN FUTURE
                    std::cout << "Added 1/2 s to fetch delay. \nTrying to fetch Page " << page + 1 << " again..." << std::endl;
                    page -= 1;
                }
                else
                {
                    std::cout << "End of Guild results reached" << std::endl;
                    break;
                }
            }
            
            offset += results;
            page++;
            
            // if you want it to be faster edit this
            randDelay(delayBetweenFetch, 0.6f);
            
        } catch (const std::exception& e) {
            std::cout << "J err: " << e.what() << std::endl;
            break;
        }
    }

    return result;
}

json fetchMsgsDM(const std::string& query) {
    json result = json::array();
    int offset = 0;
    int page = 0;

    while (true) {
        std::string url = "https://discord.com/api/v9/channels/";
        url += TARGET_ID;
        url += "/messages/search?";
        url += query;
        url += "&offset=";
        url += std::to_string(offset);

        if (verbose_fetch) {
            std::cout << "Fetching DM page " << page + 1 << " (" << offset << " offset)" << std::endl;
        }
        
        auto [status, response] = dcReq(url);
        
        if (status == 400) {
            std::cout << "Reached end of results (400 Bad Request)" << std::endl;
            break;
        }
        if (status != 200) {
            std::cout << "Err fetching messages: HTTP " << status << std::endl;
            break;
        }

        try {
            json data = json::parse(response);
            const int results = static_cast<int>(data["messages"].size());
            
            if (verbose_fetch) {
                std::cout << "Page " << page + 1 << ": Found " << results << " more messages." << std::endl;
            }

            for (auto& group : data["messages"]) {
                auto& msg = group[0];
                json entry = {
                    {"id", msg["id"].get<std::string>()},
                    {"channel_id", TARGET_ID}, // channel_id is same as target
                    {"deleted", false},
                    {"last_attempt", 0}
                };
                result.push_back(entry);
            }
            saveProgress(result);
            
            if (results == 0) {
                std::cout << "End of DM results reached" << std::endl;
                break;
            }
            
            offset += results;
            page++;
            randDelay(2.0f, 0.6f);
            
        } catch (const std::exception& e) {
            std::cout << "J err: " << e.what() << std::endl;
            break;
        }
    }
    return result;
}