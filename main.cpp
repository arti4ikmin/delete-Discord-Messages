#include <wx/wx.h>
#include <wx/textctrl.h>
#include <wx/sizer.h>
#include <wx/dialog.h>
#include <wx/thread.h>
#include <fstream>
#include <string>
#include <thread>
#include <random>
#include <iomanip>
#include <utility>
#include <vector>
#include <filesystem>
#include <atomic>
#include <chrono>
#include <nlohmann/json.hpp>
#include "dcWebHook.h"
#include "fetcher.h"
#include "deletion.h"
#include "main.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string TOKEN;
std::string TARGET_ID;
std::string QUERY;
std::string SAVE_FILE;
bool is_dm_mode = false;

// flag for output control real
bool verbose_fetch = true;

// for name
std::string global_SAVE_FILE;

// presets handler, if exists and non-empty return content
std::string readPresetValue(const std::string &fName) {
    if (fs::exists(fName)) {
        std::ifstream ifs(fName);
        std::string value;
        std::getline(ifs, value);
        if (!value.empty())
            return value;
    }
    return "";
}

void writePresetValue(const std::string &filename, const std::string &value) {
    std::ofstream ofs(filename);
    ofs << value;
}

void randDelay(const float base, const float jit) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0, jit);
    const double delay = base + dis(gen);
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(delay * 1000)));
}

void saveProgress(const json& messages) {
    std::ofstream file(global_SAVE_FILE);
    file << messages.dump(4);
}

// stream buffer to redirect cout to a wxTextCtrl
class GuiCoutBuffer final : public std::streambuf {
public:
    // constructor takes pointer to the wxTextCtrl to upd
    explicit GuiCoutBuffer(wxTextCtrl* ctrl) : m_ctrl(ctrl) {}

protected:
    // overflow prevention + flushing
    int overflow(int c) override {
        if (c != EOF) {
            char ch = static_cast<char>(c);
            m_buffer.push_back(ch);
            if (ch == '\n')
                flushBuffer();
        }
        return c;
    }

    // xsputn (putin real) to process multiple characters.
    std::streamsize xsputn(const char* s, std::streamsize count) override {
        std::streamsize written = 0;
        for (std::streamsize i = 0; i < count; ++i) {
            overflow(s[i]);
            ++written;
        }
        return written;
    }

    //
    int sync() override {
        flushBuffer();
        return 0;
    }

private:
    void flushBuffer() {
        if (!m_buffer.empty() && m_ctrl) {
            std::string text = m_buffer;
            m_buffer.clear();
            //event poster for update
            auto* evt = new wxThreadEvent(wxEVT_THREAD, wxID_ANY);
            evt->SetString(text);
            wxQueueEvent(m_ctrl, evt);
        }
    }
    wxTextCtrl* m_ctrl;
    std::string m_buffer;
};



class MissingValueDialog final : public wxDialog {
public:
    MissingValueDialog(wxWindow* parent, const wxString &prompt)
        : wxDialog(parent, wxID_ANY, "Input Required", wxDefaultPosition, wxSize(350, 150))
    {
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(new wxStaticText(this, wxID_ANY, prompt), 0, wxALL | wxEXPAND, 10);
        m_valueCtrl = new wxTextCtrl(this, wxID_ANY);
        sizer->Add(m_valueCtrl, 0, wxALL | wxEXPAND, 10);
        auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
        btnSizer->AddStretchSpacer();
        btnSizer->Add(new wxButton(this, wxID_OK, "OK"), 0, wxALL, 5);
        btnSizer->Add(new wxButton(this, wxID_CANCEL, "Cancel"), 0, wxALL, 5);
        sizer->Add(btnSizer, 0, wxEXPAND);
        SetSizer(sizer);
        Centre();
    }

    [[nodiscard]] wxString GetValue() const {
        return m_valueCtrl->GetValue();
    }
private:
    wxTextCtrl* m_valueCtrl;
};


class SettingsDialog final : public wxDialog {
public:
    explicit SettingsDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, "Settings", wxDefaultPosition, wxSize(600, 400))
    {
        auto* mainSizer = new wxBoxSizer(wxVERTICAL);

        auto* grid = new wxFlexGridSizer(2, 5, 10);
        grid->Add(new wxStaticText(this, wxID_ANY, "Target ID (Guild/DM):"), 0, wxALIGN_CENTER_VERTICAL);
        m_targetIDCtrl = new wxTextCtrl(this, wxID_ANY, readPresetValue("preset_targetID.txt"));
        grid->Add(m_targetIDCtrl, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Token:"), 0, wxALIGN_CENTER_VERTICAL);
        m_tokenCtrl = new wxTextCtrl(this, wxID_ANY, readPresetValue("preset_token.txt"));
        grid->Add(m_tokenCtrl, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Search Query:"), 0, wxALIGN_CENTER_VERTICAL);
        m_queryCtrl = new wxTextCtrl(this, wxID_ANY, readPresetValue("preset_query.txt"));
        grid->Add(m_queryCtrl, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Deletion Confirmation:"), 0, wxALIGN_CENTER_VERTICAL);
        m_deletionConfCtrl = new wxTextCtrl(this, wxID_ANY, readPresetValue("preset_deletionConfirmation.txt"));
        grid->Add(m_deletionConfCtrl, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Delete Order (y for oldest first):"), 0, wxALIGN_CENTER_VERTICAL);
        m_reverseCinCtrl = new wxTextCtrl(this, wxID_ANY, readPresetValue("preset_reverseCin.txt"));
        grid->Add(m_reverseCinCtrl, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Webhook URL:"), 0, wxALIGN_CENTER_VERTICAL);
        m_webhookCtrl = new wxTextCtrl(this, wxID_ANY, readPresetValue("preset_webhook.txt"));
        grid->Add(m_webhookCtrl, 1, wxEXPAND);

        grid->AddGrowableCol(1, 1);
        mainSizer->Add(grid, 1, wxALL | wxEXPAND, 10);

        auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
        btnSizer->AddStretchSpacer();
        btnSizer->Add(new wxButton(this, wxID_OK, "Save"), 0, wxALL, 5);
        btnSizer->Add(new wxButton(this, wxID_CANCEL, "Cancel"), 0, wxALL, 5);
        mainSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 10);

        SetSizer(mainSizer);
        Centre();
    }

    void SaveSettings() const
    {
        writePresetValue("preset_targetID.txt", std::string(m_targetIDCtrl->GetValue().mb_str()));
        writePresetValue("preset_token.txt", std::string(m_tokenCtrl->GetValue().mb_str()));
        writePresetValue("preset_query.txt", std::string(m_queryCtrl->GetValue().mb_str()));
        writePresetValue("preset_deletionConfirmation.txt", std::string(m_deletionConfCtrl->GetValue().mb_str()));
        writePresetValue("preset_reverseCin.txt", std::string(m_reverseCinCtrl->GetValue().mb_str()));
        writePresetValue("preset_webhook.txt", std::string(m_webhookCtrl->GetValue().mb_str()));
    }

private:
    wxTextCtrl *m_targetIDCtrl, *m_tokenCtrl, *m_queryCtrl, *m_deletionConfCtrl, *m_reverseCinCtrl, *m_webhookCtrl;
};


class MainWindow final : public wxFrame {
public:
    explicit MainWindow(const wxString &title)
        : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(700, 500)), m_thread(nullptr)
    {
        auto* mainSizer = new wxBoxSizer(wxVERTICAL);

        auto* menuBar = new wxMenuBar();
        auto* fileMenu = new wxMenu();
        wxMenuItem* settingsItem = fileMenu->Append(wxID_ANY, "Settings");
        fileMenu->Append(wxID_EXIT, "Exit");
        menuBar->Append(fileMenu, "More");
        wxFrameBase::SetMenuBar(menuBar);

        //
        Bind(wxEVT_MENU, &MainWindow::OnSettings, this, settingsItem->GetId());
        Bind(wxEVT_MENU, &MainWindow::OnExit, this, wxID_EXIT);

        // output window, wxTE_READONLY
        m_outputCtrl = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE | wxTE_READONLY);
        mainSizer->Add(m_outputCtrl, 1, wxEXPAND | wxALL, 5);

        // event for update on buffer
        m_outputCtrl->Bind(wxEVT_THREAD, &MainWindow::OnGuiCout, this);

        auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
        m_startButton = new wxButton(this, wxID_ANY, "Start");
        m_pauseButton = new wxButton(this, wxID_ANY, "Pause");
        m_pauseButton->Enable(false);
        btnSizer->Add(m_startButton, 0, wxALL, 5);
        btnSizer->Add(m_pauseButton, 0, wxALL, 5);
        mainSizer->Add(btnSizer, 0, wxALIGN_CENTER);

        SetSizer(mainSizer);
        Centre();

        //
        m_startButton->Bind(wxEVT_BUTTON, &MainWindow::OnStart, this);
        m_pauseButton->Bind(wxEVT_BUTTON, &MainWindow::OnPause, this);

        // cout redirector real
        static GuiCoutBuffer guiBuf(m_outputCtrl);
        std::cout.rdbuf(&guiBuf);
        
        AppendLog("--------------------------------------------------------------\nIf its not your first time, its worth checking out the presets that will be used in this program. More -> Settings. \nMaybe you want to change something...\n--------------------------------------------------------------\n");
        AppendLog("[DEBUG, VALUES:]\n");
        AppendLog("Target Channel/Guild ID: " + readPresetValue("preset_targetID.txt") + "\nQuery: " + readPresetValue("preset_query.txt") + "\n--------------------------------------------------------------\n");
    }

    ~MainWindow() override
    {
        if(m_thread) {
            m_thread->Delete();
            m_thread->Wait();
            delete m_thread;
        }
    }

    // wxQueueEvent catcher (cout)
    void OnGuiCout(const wxThreadEvent &e) {
        m_outputCtrl->AppendText(e.GetString());
    }
    
    void AppendLog(const wxString &msg) const
    {
        auto* evt = new wxThreadEvent(wxEVT_THREAD, wxID_ANY);
        evt->SetString(msg);
        wxQueueEvent(m_outputCtrl, evt);
    }

private:
    wxTextCtrl* m_outputCtrl;
    wxButton* m_startButton;
    wxButton* m_pauseButton;
    wxThread* m_thread;
    std::atomic_bool m_paused {false};
    
    class DeletionThread final : public wxThread {
    public:
        DeletionThread(MainWindow* handler, const bool is_dm, std::string targetID, std::string token, std::string query, std::string deletionConf, std::string reverseCin, std::string webhook, bool resume)
            : wxThread(wxTHREAD_JOINABLE), m_handler(handler), m_is_dm(is_dm), m_targetID(std::move(targetID)),
              m_token(std::move(token)), m_query(std::move(query)), m_deletionConf(std::move(deletionConf)), m_reverseCin(std::move(reverseCin)),
              m_webhook(std::move(webhook)), m_resume(resume)
        {
        }

    protected:
        ExitCode Entry() override {
            const std::string saveFile = (m_is_dm ? "dm_" : "guild_") + m_targetID + "_msgs.json";
            global_SAVE_FILE = saveFile;
            json msgs;

            if(m_resume) {
                if(std::ifstream file(saveFile); file) {
                    file >> msgs;
                    m_handler->AppendLog("Loaded " + std::to_string(msgs.size()) + " messages from save file.\n");
                } else {
                    m_handler->AppendLog("Err: Resume selected but save file not found (bro how tf :skull:)\n");
                    return nullptr;
                }
            } else {
                m_handler->AppendLog("Starting message fetch...\n");
                if(m_is_dm)
                    msgs = fetchMsgsDM(m_query);
                else
                    msgs = fetchMsgsGuild(m_query);
                saveProgress(msgs);
                const float est_time = static_cast<float>(msgs.size()) * 1.8f / 60; // TODO: UPDATE IF CHANGED
                m_handler->AppendLog("Total messages found: " + std::to_string(msgs.size()) + ". This will take about " + std::to_string(est_time) + " minutes\n");
                sendHookMsg("@here \nTotal messages found: " + std::to_string(msgs.size()) + ". This will take about " + std::to_string(est_time) + " minutes");
            }

            const bool reverseDel = (m_reverseCin == "y" || m_reverseCin == "Y");
            m_handler->AppendLog("Starting deletion process...\n");
            
            delMsgs(msgs, reverseDel);

            bool all_deleted = true;
            for (const auto& msg : msgs) {
                if (!msg["deleted"].get<bool>()) {
                    all_deleted = false;
                    break;
                }
            }
            if(all_deleted && fs::exists(saveFile)) {
                fs::remove(saveFile);
                m_handler->AppendLog("All messages deleted, save file removed\n");
            }
            m_handler->AppendLog("Deletion completed! Processed " + std::to_string(msgs.size()) + " messages\n");
            sendHookMsg("Deletion completed! Processed " + std::to_string(msgs.size()) + " messages");
            return nullptr;
        }
    private:
        MainWindow* m_handler;
        bool m_is_dm;
        std::string m_targetID;
        std::string m_token;
        std::string m_query;
        std::string m_deletionConf;
        std::string m_reverseCin;
        std::string m_webhook;
        bool m_resume;
    };


    void OnStart(wxCommandEvent &event) {
        m_startButton->Enable(false);
        m_pauseButton->Enable(true);

        std::string targetID = readPresetValue("preset_targetID.txt");
        if(targetID.empty()){
            if(MissingValueDialog dlg(this, "Enter Target ID (Guild/DM):"); dlg.ShowModal() == wxID_OK) {
                targetID = std::string(dlg.GetValue().mb_str());
                writePresetValue("preset_targetID.txt", targetID);
            } else {
                AppendLog("Operation cancelled, missing Target ID.\n");
                m_startButton->Enable(true);
                m_pauseButton->Enable(false);
                return;
            }
        }
        std::string token = readPresetValue("preset_token.txt");
        if(token.empty()){
            if(MissingValueDialog dlg(this, "Enter Token:"); dlg.ShowModal() == wxID_OK) {
                token = std::string(dlg.GetValue().mb_str());
                writePresetValue("preset_token.txt", token);
            } else {
                AppendLog("Operation cancelled, missing Token.\n");
                m_startButton->Enable(true);
                m_pauseButton->Enable(false);
                return;
            }
        }
        std::string query = readPresetValue("preset_query.txt");
        if(query.empty()){
            if(MissingValueDialog dlg(this, "Enter Search Query (e.g., author_id=1234&contains=hello):"); dlg.ShowModal() == wxID_OK) {
                query = std::string(dlg.GetValue().mb_str());
                writePresetValue("preset_query.txt", query);
            } else {
                AppendLog("Operation cancelled, Missing Query\n(use https://arti4ikmin.github.io/queries/ if you dont know how)");
                m_startButton->Enable(true);
                m_pauseButton->Enable(false);
                return;
            }
        }
        std::string deletionConf = readPresetValue("preset_deletionConfirmation.txt");
        if(deletionConf.empty()){
            if(MissingValueDialog dlg(this, "Type 'sure' to confirm deletion:"); dlg.ShowModal() == wxID_OK) {
                deletionConf = std::string(dlg.GetValue().mb_str());
                writePresetValue("preset_deletionConfirmation.txt", deletionConf);
            } else {
                AppendLog("Operation cancelled, Missing Deletion Confirmation\n");
                m_startButton->Enable(true);
                m_pauseButton->Enable(false);
                return;
            }
        }
        if(deletionConf != "sure") {
            AppendLog("Deletion not confirmed, Operation cancelled\n");
            m_startButton->Enable(true);
            m_pauseButton->Enable(false);
            return;
        }
        std::string reverseCin = readPresetValue("preset_reverseCin.txt");
        if(reverseCin.empty()){
            if(MissingValueDialog dlg(this, "Delete from oldest to newest? (y/n):"); dlg.ShowModal() == wxID_OK) {
                reverseCin = std::string(dlg.GetValue().mb_str());
                writePresetValue("preset_reverseCin.txt", reverseCin);
            } else {
                AppendLog("Operation cancelled, Missing Delete Order setting.\n");
                m_startButton->Enable(true);
                m_pauseButton->Enable(false);
                return;
            }
        }
        std::string webhook = readPresetValue("preset_webhook.txt");
        if(webhook.empty()){
            MissingValueDialog dlg(this, "Enter Webhook URL:");
            webhook = std::string(dlg.GetValue().mb_str());
            writePresetValue("preset_webhook.txt", webhook);
        }

        const int mode = wxMessageBox("Delete messages in:\nYes: Guild\nNo: Direct Messages", "Select Mode", wxYES_NO | wxICON_QUESTION);
        const bool is_dm = (mode == wxNO);
        const std::string saveFile = (is_dm ? "dm_" : "guild_") + targetID + "_msgs.json";
        bool resume = false;
        if(fs::exists(saveFile)) {
            const int res = wxMessageBox("Found existing save file. Resume?", "Resume", wxYES_NO | wxICON_QUESTION);
            resume = (res == wxYES);
            if(!resume)
                fs::remove(saveFile);
        }

        TARGET_ID = targetID;
        TOKEN = token;
        QUERY = query;
        is_dm_mode = is_dm;
        SAVE_FILE = saveFile;

        m_thread = new DeletionThread(this, is_dm, targetID, token, query, deletionConf, reverseCin, webhook, resume);
        if(m_thread->Run() != wxTHREAD_NO_ERROR) {
            AppendLog("Failed to start deletion thread\n");
            delete m_thread;
            m_thread = nullptr;
            m_startButton->Enable(true);
            m_pauseButton->Enable(false);
        }
    }

    void OnPause(wxCommandEvent &event) {
        if(m_paused.load()) {
            m_paused.store(false);
            m_pauseButton->SetLabel("Pause");
            AppendLog("(Button has no functionality rn lol, just for testing)");
        } else {
            m_paused.store(true);
            m_pauseButton->SetLabel("Resume");
            AppendLog("(Button has no functionality rn lol, just for testing)");
        }
    }

    void OnSettings(wxCommandEvent &event) {
        if(SettingsDialog dlg(this); dlg.ShowModal() == wxID_OK) {
            dlg.SaveSettings();
            AppendLog("Settings saved\n");
        }
    }

    void OnExit(wxCommandEvent &event) {
        Close();
    }
};

class DeleteDiscordMessages final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new MainWindow("Discord Message Deleter GUI");
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(DeleteDiscordMessages);
