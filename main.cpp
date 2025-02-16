#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <random>
#include <iomanip>
#include <vector>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "dcWebHook.h"
#include "fetcher.h"
#include "deletion.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

std::string TOKEN;
std::string TARGET_ID;
std::string QUERY;
std::string SAVE_FILE;
bool is_dm_mode = false;

// flag for output control real
bool verbose_fetch = true;


std::string readPresetValue(const std::string &presetFilename) {
    if (fs::exists(presetFilename)) {
        std::ifstream ifs(presetFilename);
        std::string value;
        std::getline(ifs, value);
        if (!value.empty())
            return value;
    }
    return "";
}

// presets handler, if exists and non-empty return content
std::string getPresetInput(const std::string &presetFilename, const std::string &prompt) {
    if (!fs::exists(presetFilename)) {
        std::ofstream ofs(presetFilename);
        ofs.close();
    }

    if (std::string val = readPresetValue(presetFilename); !val.empty()) {
        std::cout << prompt << val << " (-- Value was autofilled from preset)" << std::endl;
        return val;
    } else {
        std::cout << prompt;
        std::getline(std::cin, val);
        return val;
    }
}

void randDelay(const float base, const float jit) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0, jit);
    const double delay = base + dis(gen);
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(delay * 1000)));
}

void saveProgress(const json& messages) {
    std::ofstream file(SAVE_FILE);
    file << messages.dump(4);
    if (verbose_fetch) {
        std::cout << "Progress saved to " << SAVE_FILE << std::endl;
    }
}

int main() {
    // preset table
    std::vector<std::pair<std::string, std::string>> presets;
    std::string tmp;
    
    tmp = readPresetValue("preset_targetID.txt");
    if (!tmp.empty())
        presets.push_back({"Guild/Channel ID", tmp});
    
    tmp = readPresetValue("preset_token.txt");
    if (!tmp.empty())
        presets.push_back({"Token", tmp});
    
    tmp = readPresetValue("preset_query.txt");
    if (!tmp.empty())
        presets.push_back({"Search Query", tmp});
    
    tmp = readPresetValue("preset_deletionConfirmation.txt");
    if (!tmp.empty())
        presets.push_back({"Deletion Confirmation", tmp});
    
    tmp = readPresetValue("preset_reverseCin.txt");
    if (!tmp.empty())
        presets.push_back({"Delete Old messages first (From old to new)", tmp});
    
    if (!presets.empty()) {
        std::cout << "These values will be autofilled from presets:" << std::endl;
        std::cout << "-----------------------------------------------" << std::endl;
        for (const auto &entry : presets) {
            std::cout << entry.first << ": " << entry.second << std::endl;
        }
        std::cout << "-----------------------------------------------" << std::endl << std::endl;
    }
    // end preset table

    std::cout << "Delete messages in:\n1. Guild\n2. Direct Messages\nChoose (1/2): ";
    int choice;
    std::cin >> choice;
    std::cin.ignore();

    is_dm_mode = (choice == 2);
    std::string target_type = is_dm_mode ? "DM channel" : "guild";
    
    TARGET_ID = getPresetInput("preset_targetID.txt", "Please enter the " + target_type + " ID: ");
    
    SAVE_FILE = (is_dm_mode ? "dm_" : "guild_") + TARGET_ID + "_msgs.json"; // Construct save file name.
    json msgs;

    if (fs::exists(SAVE_FILE)) {
        std::cout << "Found existing save file. Resume? (y/n): ";
        std::string resume_choice;
        std::cin >> resume_choice;
        std::cin.ignore();

        if (resume_choice == "y" || resume_choice == "Y") {
            std::ifstream file(SAVE_FILE);
            file >> msgs;
            std::cout << "Loaded " << msgs.size() << " msgs from save file. (-- NOTICE -- You might get a few 404s, it is normal unless this exceeds about 20 times,\n \t\t\tif it does so, try refetching or checking if you have any messages left to delete.)" << std::endl;

            TOKEN = getPresetInput("preset_token.txt", "Enter your token: ");
        } else {
            fs::remove(SAVE_FILE);
        }
    }

    if (!fs::exists(SAVE_FILE)) {
        QUERY = getPresetInput("preset_query.txt", "Enter search query (e.g., author_id=1234&contains=hello): ");

        TOKEN = getPresetInput("preset_token.txt", "Enter your token: ");

        std::cout << "\nStarting messages fetch..." << std::endl;
        msgs = is_dm_mode ? fetchMsgsDM(QUERY) : fetchMsgsGuild(QUERY);
        saveProgress(msgs);
        std::cout << "\nTotal messages found: " << msgs.size() << std::endl;
    }
    
    if (!msgs.empty()) {
        const float est_time = static_cast<float>(msgs.size()) * 1.1f / 60;
        std::cout << "\nWARNING: Operation will take ~" << std::fixed << std::setprecision(1) 
                  << est_time << " minutes" << std::endl;
        sendHookMsg("@here \nTotal messages found: " + std::to_string(msgs.size()) + ". This will take about " + std::to_string(est_time) + " minutes. (If no preset, type sure in console to proceed)");
        
        std::string confirm = getPresetInput("preset_deletionConfirmation.txt", "Type 'sure' to confirm deletion: ");
        
        if (confirm == "sure") {
            std::string reverseCin = getPresetInput("preset_reverseCin.txt", "Delete from oldest to newest? (y/n): ");
            bool reverseDel = (reverseCin == "y" || reverseCin == "Y");
            
            std::cout << "\nStarting deletion process..." << std::endl;
            delMsgs(msgs, reverseDel);
            
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
            sendHookMsg("Deletion completed! Processed " + std::to_string(msgs.size()) + " messages.");
        } else {
            std::cout << "Deletion cancelled...\n";
        }
    }

    return 0;
}
