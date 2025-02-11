﻿#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <random>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

std::string TOKEN;
std::string GUILD_ID;
std::string QUERY;
std::string SAVE_FILE;

// flag for output control real
bool verbose_fetch = true;

struct MemoryStruct {
    char* memory;
    size_t size;
};

void randDelay(const float base, const float jit) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0, jit);
    const double delay = base + dis(gen);
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(delay * 1000)));
}

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

std::pair<long, std::string> dcReq(const std::string& url, const std::string& method = "GET") {
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
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
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

json fetchMsgs(const std::string& query) {
    json result = json::array();
    int offset = 0;
    int page = 0;

    while (true) {

        // compiler tells "String concatenation results in allocation of unnecessary temporary strings; consider using 'operator+='", so Ive decided to just make as it says lol
        std::string url = "https://discord.com/api/v9/guilds/";
        url += GUILD_ID;
        url += "/messages/search?";
        url += query;
        url += "&offset=";
        url += std::to_string(offset);

        
        if (verbose_fetch) {
            std::cout << "Fetching page " << page + 1 << " (" << offset << " offset)" << std::endl;
        }
        
        auto [status, response] = dcReq(url);
        
        if (status == 400) {
            std::cout << "Reached end of results (400 Bad Request)" << std::endl;
            break;
        }
        if (status != 200) {
            std::cerr << "Err fetching messages: HTTP " << status << std::endl;
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

            if (results == 0) {
                std::cout << "End of results reached" << std::endl;
                break;
            }
            
            offset += results;
            page++;
            
            // if you want it to be faster edit this
            randDelay(3.0f, 0.8f);
            
        } catch (const std::exception& e) {
            std::cerr << "J err: " << e.what() << std::endl;
            break;
        }
    }

    return result;
}

void saveProgress(const json& messages) {
    std::ofstream file(SAVE_FILE);
    file << messages.dump(4);
    if (verbose_fetch) {
        std::cout << "Progress saved to " << SAVE_FILE << std::endl;
    }
}


void delMsgs(json& messages) {
    const size_t total = messages.size();
    int processed = 0;
    bool needsSaving = false;

    for (auto& msg : messages) {
        if (msg["deleted"].get<bool>()) {
            processed++;
            continue;
        }

        constexpr float base_delay = 1.5f;

        // same shit as before "unnecessary temporary string allocations" real
        std::string url = "https://discord.com/api/v9/channels/";
        url += msg["channel_id"].get<std::string>();
        url += "/messages/";
        url += msg["id"].get<std::string>();
        
        auto [status, response] = dcReq(url, "DELETE");
        
        if (status == 204) {
            msg["deleted"] = true;
            needsSaving = true;
            
            if (processed % 10 == 0) {
                std::cout << "[" << processed + 1 << "/" << total << "] Deleted message " 
                         << msg["id"].get<std::string>() << std::endl;
                //if (needsSaving) {  }
                saveProgress(messages);
                needsSaving = false;
                
            }
        } else {
            msg["last_attempt"] = time(nullptr);
            std::cerr << "Failed to delete " << msg["id"].get<std::string>() 
                     << " (HTTP " << status << ")" << std::endl;
        }

        processed++;
        randDelay(base_delay, 0.5f);
    }

    if (needsSaving) {
        saveProgress(messages);
    }
}

int main() {
    std::cout << "Please enter the guild ID you want your msgs to be deleted in: ";
    std::getline(std::cin, GUILD_ID);

    
    SAVE_FILE = "guild_" + GUILD_ID + "_msgs.json";
    json msgs;

    if (fs::exists(SAVE_FILE)) {
        std::cout << "Found existing save file. Resume? (y/n): ";
        std::string choice;
        std::cin >> choice;

        std::cin.ignore();
        if (choice == "y" || choice == "Y") {
            std::ifstream file(SAVE_FILE);
            file >> msgs;
            std::cout << "Loaded " << msgs.size() << " msgs from save file" << std::endl;

            std::cout << "Enter your token: ";
            std::getline(std::cin, TOKEN);
        } else {
            fs::remove(SAVE_FILE);
        }
    }

    if (!fs::exists(SAVE_FILE)) {
        std::cout << "Enter search query (e.g., author_id=1234&contains=hello): ";
        std::getline(std::cin, QUERY);

        std::cout << "Enter your token: ";
        std::getline(std::cin, TOKEN);

        std::cout << "\nStarting msgs fetch... (amount of pages ≈ amount of messages / 25)" << std::endl;
        msgs = fetchMsgs(QUERY);
        saveProgress(msgs);
        std::cout << "\nTotal msgs found: " << msgs.size() << std::endl;
    }

    if (!msgs.empty()) {
        const float est_time = static_cast<float>(msgs.size()) * 1.8f / 60;
        std::cout << "\nWARNING: Operation will take ~" << std::fixed << std::setprecision(1) 
                 << est_time << " minutes" << std::endl;;
        std::cout << "Type 'sure' to confirm deletion: ";
        std::string confirm;
        std::cin >> confirm;
        
        if (confirm == "sure") {
            std::cout << "\nStarting deletion process... (progress will be shown every 10 messages)" << std::endl;
            
            delMsgs(msgs);
            
            // Clean up if all deleted
            bool all_deleted = true;
            for (const auto& msg : msgs) {
                if (!msg["deleted"].get<bool>()) {
                    all_deleted = false;
                    break;
                }
            }
            
            if (all_deleted) {
                fs::remove(SAVE_FILE);
                std::cout << "Cleanup: Removed save file\n";
            }
            
            std::cout << "\nDeletion completed! Processed " << msgs.size() << " messages.\n";
        } else {
            std::cout << "Deletion cancelled...\n";
        }
    }

    return 0;
}