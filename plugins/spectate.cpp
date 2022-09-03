//
// Created by josh on 7/28/21.
//

#include "Core.h"
#include <modules/Gui.h>
#include <Console.h>
#include <Export.h>
#include <PluginManager.h>
#include <modules/World.h>
#include <modules/EventManager.h>
#include <modules/Job.h>
#include <modules/Units.h>
#include <df/job.h>
#include <df/unit.h>
#include <df/historical_figure.h>
#include <df/global_objects.h>
#include <df/world.h>
#include <df/viewscreen.h>

#include <map>
#include <set>
#include <random>

std::map<uint16_t,uint16_t> freq;
std::set<int32_t> job_tracker;
std::default_random_engine RNG;

//#include "df/world.h"

using namespace DFHack;
using namespace df::enums;

DFHACK_PLUGIN("spectate");
DFHACK_PLUGIN_IS_ENABLED(enabled);
Pausing::AnnouncementLock* pause_lock = nullptr;
bool lock_collision = false;
bool unpause_enabled = false;
bool disengage_enabled = false;
bool focus_jobs_enabled = false;
bool following_dwarf = false;
df::unit* our_dorf = nullptr;
df::job* job_watched = nullptr;
int32_t timestamp = -1;
uint64_t tick_span = 50;
REQUIRE_GLOBAL(world);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(pause_state);
REQUIRE_GLOBAL(d_init);
#define base 0.99

command_result spectate (color_ostream &out, std::vector <std::string> & parameters);

DFhackCExport command_result plugin_init (color_ostream &out, std::vector <PluginCommand> &commands) {
    commands.push_back(PluginCommand("spectate",
                                     "Automated spectator mode.",
                                     spectate,
                                     false));
    pause_lock = World::AcquireAnnouncementPauseLock("spectate");
    return CR_OK;
}

DFhackCExport command_result plugin_onstatechange(color_ostream &out, state_change_event event) {
    if (enabled && world) {
        switch (event) {
            case SC_MAP_UNLOADED:
            case SC_BEGIN_UNLOAD:
            case SC_WORLD_UNLOADED:
                our_dorf = nullptr;
                job_watched = nullptr;
                following_dwarf = false;
            default:
                break;
        }
    }
    return CR_OK;
}

DFhackCExport command_result plugin_shutdown (color_ostream &out) {
    World::ReleasePauseLock(pause_lock);
    return CR_OK;
}

void onTick(color_ostream& out, void* tick);
void onJobStart(color_ostream &out, void* job);
void onJobCompletion(color_ostream &out, void* job);

void enable_auto_unpause(color_ostream &out, bool state){
    if(unpause_enabled != state && lock_collision) {
        // when enabled, lock collision means announcements haven't been disabled
        // when disabled, lock collision means announcement are still disabled
        // the only state left to consider here is what the lock should be set to
        lock_collision = false;
        unpause_enabled = state;
        if (unpause_enabled) {
            pause_lock->lock();
        } else {
            // this one should be redundant, the lock should already be unlocked right now
            pause_lock->unlock();
        }
        out.print(unpause_enabled ? "auto-unpause: on\n" : "auto-unpause: off\n");
        return;
    }
    unpause_enabled = state;
    // update the announcement settings if we can
    if (unpause_enabled) {
        if (World::SaveAnnouncementSettings()) {
            World::DisableAnnouncementPausing();
            pause_lock->lock();
        } else {
            lock_collision = true;
        }
    } else {
        pause_lock->unlock();
        if (!World::RestoreAnnouncementSettings()) {
            // this in theory shouldn't happen, if others use the lock like we do in spectate
            lock_collision = true;
        }
    }
    // report to the user how things went
    if (!lock_collision){
        out.print(unpause_enabled ? "auto-unpause: on\n" : "auto-unpause: off\n");
    } else {
        out.print("auto-unpause: must wait for another Pausing::AnnouncementLock to be lifted. This setting will complete when the lock lifts.\n");
    }
}

DFhackCExport command_result plugin_enable(color_ostream &out, bool enable) {
    namespace EM = EventManager;
    if (enable && !enabled) {
        out.print("Spectate mode enabled!\n");
        using namespace EM::EventType;
        EM::EventHandler ticking(onTick, 15);
        EM::EventHandler start(onJobStart, 0);
        EM::EventHandler complete(onJobCompletion, 0);
        EM::registerListener(EventType::TICK, ticking, plugin_self);
        EM::registerListener(EventType::JOB_STARTED, start, plugin_self);
        EM::registerListener(EventType::JOB_COMPLETED, complete, plugin_self);
    } else if (!enable && enabled) {
        // warp 8, engage!
        out.print("Spectate mode disabled!\n");
        EM::unregisterAll(plugin_self);
        job_tracker.clear();
        freq.clear();
    }
    enabled = enable;
    return DFHack::CR_OK;
}

DFhackCExport command_result plugin_onupdate(color_ostream &out) {
    if (lock_collision) {
        if (unpause_enabled) {
            // player asked for auto-unpause enabled
            World::SaveAnnouncementSettings();
            if (World::DisableAnnouncementPausing()){
                // now that we've got what we want, we can lock it down
                lock_collision = false;
                pause_lock->lock();
            }
        } else {
            if (World::RestoreAnnouncementSettings()) {
                lock_collision = false;
            }
        }
    }
    while (unpause_enabled && !world->status.popups.empty()) {
        // dismiss announcement popup(s)
        Gui::getCurViewscreen(true)->feed_key(interface_key::CLOSE_MEGA_ANNOUNCEMENT);
    }
    if (disengage_enabled) {
        if (our_dorf && our_dorf->id != df::global::ui->follow_unit){
            plugin_enable(out, false);
        }
    }
    return DFHack::CR_OK;
}

command_result spectate (color_ostream &out, std::vector <std::string> & parameters) {
    if(!parameters.empty()) {
        if (parameters.size() % 2 != 0) {
            return DFHack::CR_WRONG_USAGE;
        }
        for (size_t i = 0; i+1 < parameters.size(); i += 2) {
            if (parameters[i] == "auto-unpause") {
                if (parameters[i+1] == "0") {
                    enable_auto_unpause(out, false);
                } else if (parameters[i+1] == "1") {
                    enable_auto_unpause(out, true);
                } else {
                    return DFHack::CR_WRONG_USAGE;
                }
            } else if (parameters[i] == "auto-disengage") {
                if (parameters[i+1] == "0") {
                    disengage_enabled = false;
                } else if (parameters[i+1] == "1") {
                    disengage_enabled = true;
                } else {
                    return DFHack::CR_WRONG_USAGE;
                }
            } else if (parameters[i] == "focus-jobs") {
                if (parameters[i+1] == "0") {
                    focus_jobs_enabled = false;
                } else if (parameters[i+1] == "1") {
                    focus_jobs_enabled = true;
                } else {
                    return DFHack::CR_WRONG_USAGE;
                }
            } else if (parameters[i] == "tick-interval") {
                try {
                    tick_span = std::stol(parameters[i + 1]);
                } catch (const std::exception &e) {
                    out.printerr("%s\n", e.what());
                }
            } else {
                return DFHack::CR_WRONG_USAGE;
            }
        }
    } else {
        out.print(enabled ? "Spectate is enabled.\n" : "Spectate is disabled.\n");
        if(enabled) {
            out.print(unpause_enabled ? "auto-unpause: on.\n" : "auto-unpause: off.\n");
        }
    }
    return DFHack::CR_OK;
}

// every tick check whether to decide to follow a dwarf
void onTick(color_ostream& out, void* ptr) {
    int32_t tick = df::global::world->frame_counter;
    if(our_dorf){
        if(!Units::isAlive(our_dorf)){
            following_dwarf = false;
            df::global::ui->follow_unit = -1;
        }
    }
    if (!following_dwarf || (focus_jobs_enabled && !job_watched) || (tick - timestamp) > (int32_t)tick_span) {
        std::vector<df::unit*> dwarves;
        for (auto unit: df::global::world->units.active) {
            if (!Units::isCitizen(unit)) {
                continue;
            }
            dwarves.push_back(unit);
        }
        std::uniform_int_distribution<uint64_t> follow_any(0, dwarves.size() - 1);
        if (df::global::ui) {
            // if you're looking at a warning about a local address escaping, it means the unit* from dwarves (which aren't local)
            our_dorf = dwarves[follow_any(RNG)];
            df::global::ui->follow_unit = our_dorf->id;
            job_watched = our_dorf->job.current_job;
            following_dwarf = true;
            if (!job_watched) {
                timestamp = tick;
            }
        }
    }
}

// every new worked job needs to be considered
void onJobStart(color_ostream& out, void* job_ptr) {
    // todo: detect mood jobs
    int32_t tick = df::global::world->frame_counter;
    auto job = (df::job*) job_ptr;
    // don't forget about it
    int zcount = ++freq[job->pos.z];
    job_tracker.emplace(job->id);
    // if we're not doing anything~ then let's pick something
    if ((focus_jobs_enabled && !job_watched) || (tick - timestamp) > (int32_t)tick_span) {
        following_dwarf = true;
        // todo: allow the user to configure b, and also revise the math
        const double b = base;
        double p = b * ((double) zcount / job_tracker.size());
        std::bernoulli_distribution follow_job(p);
        if (!job->flags.bits.special && follow_job(RNG)) {
            job_watched = job;
            df::unit* unit = Job::getWorker(job);
            if (df::global::ui && unit) {
                our_dorf = unit;
                df::global::ui->follow_unit = unit->id;
            }
        } else {
            timestamp = tick;
            std::vector<df::unit*> nonworkers;
            for (auto unit: df::global::world->units.active) {
                if (!Units::isCitizen(unit) || unit->job.current_job) {
                    continue;
                }
                nonworkers.push_back(unit);
            }
            std::uniform_int_distribution<> follow_drunk(0, nonworkers.size() - 1);
            if (df::global::ui) {
                df::global::ui->follow_unit = nonworkers[follow_drunk(RNG)]->id;
            }
        }
    }
}

// every job completed can be forgotten about
void onJobCompletion(color_ostream &out, void* job_ptr) {
    auto job = (df::job*)job_ptr;
    // forget about it
    freq[job->pos.z]--;
    freq[job->pos.z] = freq[job->pos.z] < 0 ? 0 : freq[job->pos.z];
    // the job doesn't exist, so we definitely need to get rid of that
    job_tracker.erase(job->id);
    // the event manager clones jobs and returns those clones for completed jobs. So the pointers won't match without a refactor of EM passing clones to both events
    if (job_watched->id == job->id) {
        job_watched = nullptr;
    }
}
