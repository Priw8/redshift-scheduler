#include "glibmm/miscutils.h"
#include "glibmm/refptr.h"
#include "glibmm/ustring.h"
#include "gtkmm/aboutdialog.h"
#include "gtkmm/button.h"
#include "gtkmm/cssprovider.h"
#include "gtkmm/entry.h"
#include "gtkmm/enums.h"
#include "gtkmm/label.h"
#include "gtkmm/scrolledwindow.h"
#include "gtkmm/styleprovider.h"
#include "gtkmm/switch.h"
#include "gtkmm/window.h"
#include <bits/types/struct_tm.h>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <gtkmm.h>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>

struct ScheduledTime {
    int hours;
    int minutes;
    std::string toString() const {
        std::string res;
        if (hours < 10) {
            res += "0";
        }
        res += std::to_string(hours) + std::string(":");
        if (minutes < 10) {
            res += "0";
        }
        return res + std::to_string(minutes);
    }
};

using ScheduleList = std::list<std::pair<ScheduledTime, int>>;

bool operator==(const ScheduledTime& t1, const ScheduledTime& t2) {
    return t1.hours == t2.hours && t1.minutes == t2.minutes;
}

bool operator<(const ScheduledTime& t1, const ScheduledTime& t2) {
    return t1.hours < t2.hours || (t1.hours == t2.hours && t1.minutes < t2.minutes);
}

bool operator>(const ScheduledTime& t1, const ScheduledTime& t2) {
    return t2 < t1;
}

bool operator>=(const ScheduledTime& t1, const ScheduledTime& t2) {
    return !(t1 < t2);
}

bool operator<=(const ScheduledTime& t1, const ScheduledTime& t2) {
    return !(t2 < t1);
}

bool operator==(const ScheduledTime& t1, const std::tm& t2) {
    return t1.hours == t2.tm_hour && t1.minutes == t2.tm_min;
}

bool operator<(const ScheduledTime& t1, const std::tm& t2) {
    return t1.hours < t2.tm_hour || (t1.hours == t2.tm_hour && t1.minutes < t2.tm_min);
}

bool operator>(const ScheduledTime& t1, const std::tm& t2) {
    return t1.hours > t2.tm_hour || (t1.hours == t2.tm_hour && t1.minutes > t2.tm_min);
}

bool operator>=(const ScheduledTime& t1, const std::tm& t2) {
    return !(t1 < t2);
}

bool operator<=(const ScheduledTime& t1, const std::tm& t2) {
    return !(t1 > t2);
}

template<typename T>
bool inRange(T val, T min, T max) {
    return val >= min && val <= max;
}

ScheduledTime parseTime(const char* timeString) {
    static std::regex timeRegex("([0-9]+):([0-9]+)");
    std::match_results<const char*> results;
    if (std::regex_match(timeString, results, timeRegex)) {
        int hours = std::stoi(results[1]);
        int minutes = std::stoi(results[2]);
        if (!inRange(hours, 0, 23)) {
            throw std::runtime_error("Hours must be in range: 0-23");
        }
        if (!inRange(minutes, 0, 59)) {
            throw std::runtime_error("Minutes must be in range: 0-59");
        }
        return {
            hours,
            minutes
        };
    } else {
        throw std::runtime_error("Failed to parse time");
    }
}

int parseTemperature(const char* temperatureString) {
    static std::regex temperatureRegex("([0-9]+) *K?");
    std::match_results<const char*> results;
    if (std::regex_match(temperatureString, results, temperatureRegex)) {
        int temperature = std::stoi(results[1]);
        if (!inRange(temperature, 1000, 25000)) {
            throw std::runtime_error("Temperature must be within 1000K and 25000K");
        } else {
            return temperature;
        }
    } else {
        throw std::runtime_error("Failed to parse temperature");
    }
}

char* trimString(char* str) {
    while(std::isspace(*str))
        ++str;

    int i = 0;
    while(!std::isspace(str[i]) && str[i] != '\0')
        ++i;

    str[i] = '\0';
    return str;
}

enum CommandMessageType {
    TERMINATE,
    UPDATE
};

struct CommandMessage {
    operator bool() {return true;};
    CommandMessageType type;
    ScheduleList schedule;
};

class BasicCommunication {
public:
    std::optional<CommandMessage> readCommandTimeout(const std::chrono::seconds& timeout) {
        std::unique_lock lock(commandMutex);
        std::chrono::time_point endWaitingTimePoint = std::chrono::system_clock::now() + timeout;
        if (commands.empty()) {
            commandVar.wait_until(lock, endWaitingTimePoint, [this](){
                return !commands.empty();
            });
        }
        if (commands.empty()) {
            lock.unlock();
            return std::nullopt;
        }

        CommandMessage& cmd = commands.front();
        auto moved = std::move(cmd);
        commands.pop_front();
        lock.unlock();
        return std::optional(std::move(moved));
    }
    CommandMessage readCommand() {
        std::unique_lock lock(commandMutex);
        if (commands.empty()) {
            commandVar.wait(lock, [this](){
                return !commands.empty();
            });
        }

        CommandMessage& cmd = commands.front();
        auto moved = std::move(cmd);
        commands.pop_front();
        lock.unlock();
        return moved;
    }
    void writeCommand(CommandMessage&& msg) {
        std::lock_guard guard(commandMutex); 
        commands.push_back(std::move(msg));
        commandVar.notify_all();
    }
private:
    std::mutex commandMutex;
    std::condition_variable commandVar;
    std::list<CommandMessage> commands;
};

BasicCommunication communication;

class ScheduledTemperature {
public:
    ScheduledTemperature() {
        box.append(entryTime);
        box.append(entryTemperature);
        box.append(btnDelete);
        entryTime.set_hexpand(true);
        entryTemperature.set_hexpand(true);
        entryTime.set_alignment(Gtk::Align::CENTER);
        entryTemperature.set_alignment(Gtk::Align::CENTER);
        entryTime.set_placeholder_text("Time (0:00-23:59)");
        entryTemperature.set_placeholder_text("Temp (1000-25000)");
        btnDelete.set_icon_name("edit-delete");
    }
    Gtk::Box& getBox() {
        return box;
    }
    Gtk::Button& getBtnDelete() {
        return btnDelete;
    }
    ScheduledTime getTime() {
        return parseTime(entryTime.get_text().c_str());
    }
    void setTime(const ScheduledTime& time) {
        entryTime.set_text(time.toString());
    }
    void setTemperature(const int temperature) {
        entryTemperature.set_text(std::to_string(temperature) + "K");
    }
    int getTemperature() {
        return parseTemperature(entryTemperature.get_text().c_str());
    }
private:
    Gtk::Box box;
    Gtk::Button btnDelete;
    Gtk::Entry entryTime;
    Gtk::Entry entryTemperature;
};

class RedshiftScheduler {
public:
    RedshiftScheduler(Glib::RefPtr<Gtk::Application> app) {
        this->app = app;
        this->builder = Gtk::Builder::create_from_file("redshift-scheduler.ui");
        this->list = builder->get_widget<Gtk::Box>("list");
        this->labelOutput = builder->get_widget<Gtk::Label>("label-output");
        this->switchEnabled = builder->get_widget<Gtk::Switch>("switch-enabled");
        // printf("AAAAAAAAAAAAAAAA\nAAAAAAAAAAAAAAAA\nAAAAAAAAAAAAAAAA\nAAAAAAAAAAAAAAAA\n");
        this->windowAbout = builder->get_widget<Gtk::Window>("wnd-about");
        this->windowLicense = builder->get_widget<Gtk::Window>("wnd-license");

        auto btnAdd = builder->get_widget<Gtk::Button>("btn-add");
        auto btnSave = builder->get_widget<Gtk::Button>("btn-save");
        auto btnAbout = builder->get_widget<Gtk::Button>("btn-about");
        auto btnLicense = builder->get_widget<Gtk::Button>("btn-license");
        auto btnAboutClose = builder->get_widget<Gtk::Button>("btn-about-close");
        auto wnd = builder->get_widget<Gtk::Window>("wnd");

        if (!this->list || !this->labelOutput || !this->switchEnabled || !btnAdd || !btnSave || !btnAbout
          || !btnLicense || !btnAboutClose || !wnd || !windowAbout || !windowLicense) {
            throw std::runtime_error("ui file is invalid or corrupt");
        }

        btnAdd->signal_clicked().connect([this]() {
            addNewEntry();
        });

        btnSave->signal_clicked().connect([this]() {
            save();
        });

        btnAbout->signal_clicked().connect([this]() {
            windowAbout->show();
        });

        btnLicense->signal_clicked().connect([this]() {
            windowLicense->show();
        });

        btnAboutClose->signal_clicked().connect([this]() {
            windowAbout->hide();
        });

        cssProvider = Gtk::CssProvider::create();
        Gtk::StyleProvider::add_provider_for_display(wnd->get_display(), cssProvider, 0);
        cssProvider->load_from_path("redshift-scheduler.css");

        loadConfigFile();
        sendUpdate();

        this->app->add_window(*wnd);
        this->app->add_window(*windowAbout);
        wnd->set_visible(true);
    }
    void setOutputLabel(const char* message) {
        labelOutput->set_label(message);
    }
    void setOutputLabel(std::string& message) {
        labelOutput->set_label(message);
    }
    bool getIsEnabled() const {
        return switchEnabled->get_active();
    }
    void setIsEnabled(bool enabled) {
        switchEnabled->set_active(enabled);
    }
    std::unique_ptr<ScheduledTemperature>& addNewEntry() {
        entries.push_back(std::make_unique<ScheduledTemperature>());
        auto& entry = entries.back();
        list->append(entry->getBox());
        entry->getBtnDelete().signal_clicked().connect([this, &entry]() {
            list->remove(entry->getBox());
            entries.remove(entry);
        });
        return entry;
    }
    void save() {
        try {
            std::pair<ScheduledTime, int> prev;
            std::string output = "# redshift-scheduler configuration file\n";

            if (!getIsEnabled()) {
                output += "\ndisabled\n";
            }

            for (auto& entry : entries) {
                auto curr = std::make_pair(entry->getTime(), entry->getTemperature());
                // If temperature is 0 it means that it's the default-initialized prev, so skip it
                if (prev.second != 0 && prev.first >= curr.first) {
                    throw std::runtime_error("Time entries must be ordered chronologically");
                }
                output += "\n" + curr.first.toString() + "\n";
                output += std::to_string(curr.second) + "K\n";
                prev = curr;
            }
            writeConfigFile(output);
            sendUpdate();
            setOutputLabel("Saved successfully");
        } catch(std::runtime_error& e) {
            setOutputLabel(e.what());
        }
    }
private:
    Glib::RefPtr<Gtk::Application> app;
    Glib::RefPtr<Gtk::CssProvider> cssProvider;
    Glib::RefPtr<Gtk::Builder> builder;
    Gtk::Window* windowAbout;    // Managed by the builder
    Gtk::Window* windowLicense;  // Managed by the builder
    Gtk::Box* list;              // Managed by the builder
    Gtk::Label* labelOutput;     // Managed by the builder
    Gtk::Switch* switchEnabled;  // Managed by the builder

    std::list<std::unique_ptr<ScheduledTemperature>> entries;

    std::string getConfigDirName() {
        return Glib::get_user_config_dir() + "/redshift-scheduler";
    }

    std::string getConfigFileName() {
        return getConfigDirName() + "/config.txt";
    }

    ScheduleList createScheduleList() {
        ScheduleList list;
        for (auto& entry : entries) {
            list.push_back({
                entry->getTime(),
                entry->getTemperature()
            });
        }
        return list;
    }

    void sendUpdate() {
        if (getIsEnabled()) {
            communication.writeCommand({
                UPDATE,
                createScheduleList()
            });
        } else {
            // If it's disabled, we can just send an empty list to the worker thread
            communication.writeCommand({UPDATE});
        }
    }

    void writeConfigFile(const std::string& cfg) {
        std::filesystem::create_directory(getConfigDirName());
        std::ofstream output(getConfigFileName());
        output << cfg;
        output.close();
    }

    void loadConfigFile() {
        if (!std::filesystem::is_regular_file(getConfigFileName())) {
            std::cout << "Config file not found!" << std::endl;
            return;
        }

        std::ifstream input(getConfigFileName());
        std::optional<ScheduledTime> time;
        std::optional<int> temperature;

        char buffer[64];
        int line = 0;
        while (input.getline(buffer, sizeof(buffer)), !(input.rdstate() & std::ifstream::failbit)) {
            ++line;
            char* commentStart = std::strchr(buffer, '#');
            if (commentStart != nullptr) {
                *commentStart = '\0';
            }
            char* trimmed = trimString(buffer);
            if (trimmed[0] == '\0')
                continue;

            try {
                if (std::strcmp(trimmed, "disabled") == 0) {
                    setIsEnabled(false);
                } else if (!time.has_value()) {
                    time = parseTime(trimmed);
                } else {
                    temperature = parseTemperature(trimmed);

                    auto& entry = addNewEntry();
                    entry->setTime(time.value());
                    entry->setTemperature(temperature.value());
                    time.reset();
                    temperature.reset();
                }
            } catch(std::runtime_error& e) {
                std::cout << "Error parsing config at line " << line << ": " << e.what() << std::endl;
                break;
            }
        };
    }
};

void invokeRedshift(int temperature) {
    std::string cmd = "redshift -P -O " + std::to_string(temperature) + " &> /dev/null";
    std::cout << "Running command: " << std::quoted(cmd) << std::endl;
    std::system(cmd.c_str());
}

std::chrono::seconds updateRedshift(const ScheduleList& list) {
    if (list.size() <= 1) {
        if (list.size() == 0) {
            invokeRedshift(6500);
        }
        if (list.size() == 1) {
            auto& entry = list.front();
            invokeRedshift(entry.second);
        }
        return std::chrono::seconds(60*60*24);
    }

    std::chrono::time_point timePoint = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(timePoint);
    std::tm* localTime = std::localtime(&time);
    
    auto& lastEntry = list.back();
    auto& firstEntry = list.front();
    if (firstEntry.first > *localTime) {
        // Use last time entry (wraparound)
        invokeRedshift(lastEntry.second);
        int secs = (firstEntry.first.hours - localTime->tm_hour)*(60*60) + (firstEntry.first.minutes - localTime->tm_min)*60 - localTime->tm_sec;
        return std::chrono::seconds(secs);
    }
    const std::pair<ScheduledTime, int>* prev = &firstEntry;
    // Start iteration from 2nd element, because the range (back,front) is already handled
    auto it = list.begin();
    it = std::next(it);
    for (; true; ++it) {
        if (it == list.end()) {
            invokeRedshift(prev->second);
            int secs = (24 - prev->first.hours)*(60*60) - localTime->tm_min*60 - localTime->tm_sec;
            return std::chrono::seconds(secs);
        }

        
        if (prev->first <= *localTime && it->first > *localTime) {
            invokeRedshift(prev->second);
            int secs = (it->first.hours - localTime->tm_hour)*(60*60) + (it->first.minutes - localTime->tm_min)*60 - localTime->tm_sec;
            return std::chrono::seconds(secs);
        }
        // Only slightly cursed
        prev = &(*it);
    }
}

int worker() {
    ScheduleList schedule;
    std::chrono::seconds nextUpdate(60*60*24);
    while(1) {
        auto cmd = communication.readCommandTimeout(nextUpdate);
        if (cmd.has_value()) {
            if (cmd->type == TERMINATE) {
                break;
            } else if (cmd->type == UPDATE) {
                schedule = cmd->schedule;
            }
        }
        nextUpdate = updateRedshift(schedule);
    }

    return 0;
}

int main(int argc, char** argv) {
    auto app = Gtk::Application::create("com.priw8-redshift-scheduler");
    std::unique_ptr<RedshiftScheduler> scheduler;

    app->signal_activate().connect([&app, &scheduler]() {
        scheduler = std::make_unique<RedshiftScheduler>(app);
    });
    
    std::thread workerThread(worker);
    int ret = app->run(argc, argv);
    communication.writeCommand({TERMINATE});
    workerThread.join();
    return ret;
}
