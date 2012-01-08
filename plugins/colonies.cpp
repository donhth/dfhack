#include "Core.h"
#include <Console.h>
#include <Export.h>
#include <PluginManager.h>
#include <vector>
#include <string>
#include <modules/Vermin.h>

using std::vector;
using std::string;
using namespace DFHack;
using namespace DFHack::Simple;
#include <DFHack.h>

DFhackCExport command_result colonies (Core * c, vector <string> & parameters);

DFhackCExport const char * plugin_name ( void )
{
    return "colonies";
}

DFhackCExport command_result plugin_init ( Core * c, std::vector <PluginCommand> &commands)
{
    commands.clear();
    commands.push_back(PluginCommand("colonies",
               "List or change wild colonies (ants hills and such)",
                colonies));
    return CR_OK;
}

DFhackCExport command_result plugin_shutdown ( Core * c )
{
    return CR_OK;
}

void destroyColonies();
void convertColonies(DFHack::Materials *Materials);
void showColonies(Core *c, DFHack::Materials *Materials);

DFhackCExport command_result colonies (Core * c, vector <string> & parameters)
{
    bool destroy = false;
    bool convert = false;
    bool help = false;

    for(int i = 0; i < parameters.size();i++)
    {
        if(parameters[i] == "kill")
            destroy = true;
        else if(parameters[i] == "bees")
            convert = true;
        else if(parameters[i] == "help" || parameters[i] == "?")
        {
            help = true;
        }
    }
    if(help)
    {
        c->con.print("Without any options, this command lists all the vermin colonies present.\n"
        "Options:\n"
        "kill   - destroy colonies\n"
        "bees   - turn colonies into honey bees\n"
        );
        return CR_OK;
    }
    if (destroy && convert)
    {
        c->con.printerr("Kill or make bees? DECIDE!\n");
        c->Resume();
        return CR_FAILURE;
    }
    c->Suspend();

    Materials * materials = c->getMaterials();

    materials->ReadCreatureTypesEx();

    if (destroy)
        destroyColonies();
    else if (convert)
        convertColonies(materials);
    else
        showColonies(c, materials);

    materials->Finish();

    c->Resume();
    return CR_OK;
}

void destroyColonies()
{
    uint32_t numSpawnPoints = Vermin::getNumVermin();
    for (uint32_t i = 0; i < numSpawnPoints; i++)
    {
        Vermin::t_vermin sp;
        Vermin::Read(i, sp);

        if (sp.in_use && Vermin::isWildColony(sp))
        {
            sp.in_use = false;
            Vermin::Write(i, sp);
        }
    }
}

// Convert all colonies to honey bees.
void convertColonies(DFHack::Materials *Materials)
{
    int bee_idx = -1;
    for (size_t i = 0; i < Materials->raceEx.size(); i++)
        if (Materials->raceEx[i].id == "HONEY_BEE")
        {
            bee_idx = i;
            break;
        }

    if (bee_idx == -1)
    {
        std::cerr << "Honey bees not present in game." << std::endl;
        return;
    }

    uint32_t numSpawnPoints = Vermin::getNumVermin();
    for (uint32_t i = 0; i < numSpawnPoints; i++)
    {
        Vermin::t_vermin sp;
        Vermin::Read(i, sp);

        if (sp.in_use && Vermin::isWildColony(sp))
        {
            sp.race = bee_idx;
            Vermin::Write(i, sp);
        }
    }
}

void showColonies(Core *c, DFHack::Materials *Materials)
{
    uint32_t numSpawnPoints = Vermin::getNumVermin();
    int      numColonies    = 0;
    for (uint32_t i = 0; i < numSpawnPoints; i++)
    {
        Vermin::t_vermin sp;

        Vermin::Read(i, sp);

        if (sp.in_use && Vermin::isWildColony(sp))
        {
            numColonies++;
            string race="(no race)";
            if(sp.race != -1)
                race = Materials->raceEx[sp.race].id;

             c->con.print("Spawn point %u: %s at %d:%d:%d\n", i,
                          race.c_str(), sp.x, sp.y, sp.z);
        }
    }

    if (numColonies == 0)
        c->con << "No colonies present." << std::endl;
}
