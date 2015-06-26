/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos.
 **
 **  Calaos is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
#include "RoomModel.h"

bool IOHitsCompare(const IOBase *lhs, const IOBase *rhs)
{
    return ((IOBase *)lhs)->params["hits"] > ((IOBase *)rhs)->params["hits"];
}

bool IOScenarioCompare(const IOBase *lhs, const IOBase *rhs)
{
    return ((IOBase *)lhs)->params["scenarioPref"] > ((IOBase *)rhs)->params["scenarioPref"];
}

bool RoomHitsCompare(const Room *lhs, const Room *rhs)
{
    return lhs->hits > rhs->hits;
}

RoomModel::RoomModel(CalaosConnection *con):
    connection(con)
{
    connection->notify_room_delete.connect(
                sigc::mem_fun(*this, &RoomModel::notifyRoomDel));
    connection->notify_room_new.connect(
                sigc::mem_fun(*this, &RoomModel::notifyRoomAdd));
}

RoomModel::~RoomModel()
{
    json_decref(jsonHome);
    for_each(rooms.begin(), rooms.end(), Delete());
}

void RoomModel::load()
{
    connection->sendCommand("get_home", Params(), sigc::mem_fun(*this, &RoomModel::home_get_cb));
}

void RoomModel::home_get_cb(json_t *result, void *data)
{
    if (!result || !json_is_object(result)) return;

    json_t *jhome = json_object_get(result, "home");
    if (!json_is_array(jhome))
        return;

    jsonHome = result;

    size_t idx;
    json_t *value;

    json_array_foreach(jhome, idx, value)
    {
        Room *room = new Room(connection, this);
        room->load(value);
        rooms.push_back(room);
    }

    cDebug() << "[ROOM load done]";

    rooms.sort(RoomHitsCompare);
    cacheScenarios.sort(IOHitsCompare);
    cacheScenariosPref = cacheScenarios;
    cacheScenariosPref.sort(IOScenarioCompare);

    updateRoomType();

    cDebug() << "[ROOM LOAD DONE sending signal]";

    load_done.emit();
}

void Room::load(json_t *data)
{
    name = jansson_string_get(data, "name", "<unkown room name>");
    type = jansson_string_get(data, "type", "<unkown type name>");
    string h = jansson_string_get(data, "hits", "0");
    Utils::from_string(h, hits);

    json_t *items = json_object_get(data, "items");
    if (!json_is_object(items))
        return;

    //load inputs
    json_t *inputs = json_object_get(items, "inputs");
    if (json_is_array(inputs))
    {
        size_t idx;
        json_t *value;

        json_array_foreach(inputs, idx, value)
        {
            loadNewIO(value, IOBase::IO_INPUT);
        }
    }

    //load outputs
    json_t *outputs = json_object_get(items, "outputs");
    if (json_is_array(outputs))
    {
        size_t idx;
        json_t *value;

        json_array_foreach(outputs, idx, value)
        {
            loadNewIO(value, IOBase::IO_OUTPUT);
        }
    }

    ios.sort(IOHitsCompare);

    updateVisibleIO();
}

void Room::loadNewIO(json_t *data, int io_type)
{
    if (jansson_string_get(data, "id") == "" ||
        jansson_string_get(data, "type") == "" ||
        jansson_string_get(data, "gui_type") == "")
        return; //don't load wrong IO

    IOBase *io = new IOBase(connection, this, io_type);
    ios.push_back(io);

    jansson_decode_object(data, io->params);

    load_io_done(io);
}

void Room::load_io_done(IOBase *io)
{
    //Put everything in cache so we can use it easily later
    if (io->io_type == IOBase::IO_INPUT)
        model->cacheInputs[io->params["id"]] = io;
    else
        model->cacheOutputs[io->params["id"]] = io;

    if (io->params["chauffage_id"] != "")
        model->chauffageList.push_back(io);

    string _type = io->params["gui_type"];

    if (_type == "scenario" && io->io_type == IOBase::IO_OUTPUT)
    {
        model->cacheScenarios.push_back(io);
    }

    if (_type == "light" ||
        _type == "light_dimmer" ||
        _type == "light_rgb")
    {
        int value;
        from_string(io->params["state"], value);

        if (io->params["state"] == "true" || value > 0)
        {
            RoomIO roomIO;
            roomIO.io = io;
            roomIO.room = this;

            model->cacheLightsOn[io] = roomIO;
        }
    }

    if (_type == "shutter" ||
        _type == "shutter_smart")
    {
        int value = 100;

        vector<string> tokens;
        split(io->params["state"], tokens);
        if (tokens.size() > 1)
            from_string(tokens[1], value);

        if (io->params["state"] == "false" || value < 100 || io->params["state"] == "down")
        {
            RoomIO roomIO;
            roomIO.io = io;
            roomIO.room = this;

            model->cacheShuttersUp[io] = roomIO;
        }
    }

    if (_type == "time_range")
    {
        io->loadPlage();
    }

    if (_type == "avreceiver")
    {
        cCritical() << "TODO: avreceiver";
        /*
         * TODO:
        io->sendUserCommand("states?", [=](bool success, vector<string> result, void *data)
        {
            if (!success) return;

            for (unsigned int i = 2;i < result.size();i++)
            {
                vector<string> tok;
                split(result[i], tok, ":", 2);

                io->params.Add(tok[0], tok[1]);
            }
        });
        io->sendUserCommand("query input_sources ?", [=](bool success, vector<string> result, void *data)
        {
            if (!success) return;

            io->amplifier_inputs.clear();
            for (unsigned int i = 4;i < result.size();i++)
            {
                vector<string> tok;
                int num;
                split(result[i], tok, ":", 2);
                from_string(tok[0], num);

                io->amplifier_inputs[num] = tok[1];
            }
        });
        */
    }
}

void Room::updateVisibleIO()
{
    visible_ios.clear();
    scenario_ios.clear();

    list<IOBase *>::iterator it = ios.begin();
    for (;it != ios.end();it++)
    {
        IOBase *io = *it;

        if (io->params["gui_type"] != "scenario" &&
            io->params.Exists("auto_scenario"))
            continue;

        if (io->params["gui_type"] == "light" ||
            (io->params["gui_type"] == "var_bool" && io->io_type == IOBase::IO_OUTPUT) ||
            (io->params["gui_type"] == "var_int" && io->io_type == IOBase::IO_OUTPUT) ||
            (io->params["gui_type"] == "var_string" && io->io_type == IOBase::IO_OUTPUT) ||
            io->params["gui_type"] == "temp" ||
            (io->params["gui_type"] == "scenario" && io->io_type == IOBase::IO_OUTPUT) ||
            io->params["gui_type"] == "analog_in" ||
            io->params["gui_type"] == "light_dimmer" ||
            io->params["gui_type"] == "light_rgb" ||
            io->params["gui_type"] == "shutter" ||
            io->params["gui_type"] == "shutter_smart" ||
            io->params["gui_type"] == "analog_out"  ||
            io->params["gui_type"] == "string_in")
        {
            if (io->params["visible"] == "true")
                visible_ios.push_back(io);

            scenario_ios.push_back(io);
        }

        if (io->params["gui_type"] == "camera_output" ||
            io->params["gui_type"] == "audio_output" ||
            io->params["gui_type"] == "avreceiver")
        {
            scenario_ios.push_back(io);
        }
    }

    visible_ios.sort(IOHitsCompare);
    scenario_ios.sort(IOHitsCompare);
}

IOBase *RoomModel::getChauffageForType(string type)
{
    list<Room *> l;
    list<Room *>::iterator it = rooms_type.begin();

    for (;it != rooms_type.end();it++)
    {
        if ((*it)->type == type)
            l.push_back((*it));
    }

    it = l.begin();
    l.sort(RoomHitsCompare);

    for (;it != l.end();it++)
    {
        IOBase *io = (*it)->getChauffage();

        if (io)
            return io;
    }

    return NULL;
}

IOBase *Room::getChauffage()
{
    list<IOBase *>::iterator it = visible_ios.begin();
    for (;it != visible_ios.end();it++)
    {
        IOBase *io = (*it);
        if (io->params["gui_type"] == "temp")
            return io;
    }

    return NULL;
}

void RoomModel::updateRoomType()
{
    rooms_type.clear();

    list<Room *>::iterator it, itr;
    for (it = rooms.begin(); it != rooms.end();it++)
    {
        Room *room = *it;

        if (room->type == "Internal")
            continue;

        bool found = false;
        for (itr = rooms_type.begin(); itr != rooms_type.end();itr++)
        {
            if ((*itr)->type == room->type)
                found = true;
        }
        if (found)
            continue;

        rooms_type.push_back(room);
    }
}

void IOBase::sendAction(string command)
{
    Params p = {{ "type", io_type == IO_INPUT?"input":"output" },
                { "id", params["id"] },
                { "value", command }};
    connection->sendCommand("set_state", p);
}

void IOBase::sendUserCommand(string command, CommandDone_cb callback, void *data)
{   
    Params p = {{ "type", io_type == IO_INPUT?"input":"output" },
                { "id", params["id"] },
                { "value", command }};
    connection->sendCommand("set_state", p, callback, data);
}

void IOBase::notifyChange(const string &msgtype, const Params &evdata)
{
    vector<string> tok;
    split(msgtype, tok);

    if (io_type == IO_INPUT && params["gui_type"] == "time_range" &&
        msgtype == "timerange_changed")
    {
        //Reload InPlageHoraire
        loadPlage();
        io_changed.emit();

        return;
    }

    if (io_type == IO_INPUT && msgtype == "input_prop_deleted")
    {
        cCritical() << "TODO, input_prop_deleted not implemented yet";
        return;
    }

    if (io_type == IO_OUTPUT && msgtype == "output_prop_deleted")
    {
        cCritical() << "TODO, output_prop_deleted not implemented yet";
        return;
    }

    if ((io_type == IO_INPUT && msgtype != "input_changed") ||
        (io_type == IO_OUTPUT && msgtype != "output_changed"))
        return;

    if (params["gui_type"] == "scenario")
    {
        if (params["ioBoolState"] == evdata["id"] && evdata.Exists("state"))
        {
            params.Add("state", evdata["state"]);
            io_changed.emit();

            return;
        }
    }

    if (evdata["id"] == params["id"])
    {
        for (int i = 0;i < evdata.size();i++)
        {
            string key, val;
            evdata.get_item(i, key, val);
            params.Add(key, val);
        }
        io_changed.emit();

        if (evdata.Exists("visible"))
            room->updateVisibleIO(); //update visibility

        checkCacheChange();

        //Also update WITemp if we change consigne
        if (params["chauffage_id"] != "" && params["gui_type"] == "var_int")
        {
            IOBase *temp = room->model->getTempFromConsigne(this);
            if (temp)
                temp->io_changed.emit();
        }
    }
}

void IOBase::checkCacheChange()
{
    if (params["gui_type"] == "light" ||
        params["gui_type"] == "light_dimmer" ||
        params["gui_type"] == "light_rgb")
    {
        int value;
        from_string(params["state"], value);

        RoomIOCache::iterator it = room->model->cacheLightsOn.find(this);

        if (params["state"] == "true" || value > 0)
        {
            if (it == room->model->cacheLightsOn.end())
            {
                RoomIO roomIO;
                roomIO.io = this;
                roomIO.room = room;

                room->model->cacheLightsOn[this] = roomIO;
                room->model->lights_on_changed.emit(room->model->cacheLightsOn.size());
            }
        }
        else
        {
            if (it != room->model->cacheLightsOn.end())
            {
                room->model->cacheLightsOn.erase(it);
                room->model->lights_on_changed.emit(room->model->cacheLightsOn.size());
            }
        }
    }

    if (params["gui_type"] == "shutter" ||
        params["gui_type"] == "shutter_smart")
    {
        int value = 100;

        vector<string> tokens;
        split(params["state"], tokens);
        if (tokens.size() > 1)
            from_string(tokens[1], value);

        RoomIOCache::iterator it = room->model->cacheShuttersUp.find(this);

        if (params["state"] == "false" || value < 100 || params["state"] == "down")
        {
            if (it == room->model->cacheShuttersUp.end())
            {
                RoomIO roomIO;
                roomIO.io = this;
                roomIO.room = room;

                room->model->cacheShuttersUp[this] = roomIO;
                room->model->shutters_up_changed.emit(room->model->cacheShuttersUp.size());
            }
        }
        else
        {
            if (it != room->model->cacheShuttersUp.end())
            {
                room->model->cacheShuttersUp.erase(it);
                room->model->shutters_up_changed.emit(room->model->cacheShuttersUp.size());
            }
        }
    }
}

void Room::notifyChange(const string &msgtype, const Params &evdata)
{
    VAR_UNUSED(msgtype);

    if (evdata.Exists("input_id_deleted") &&
        evdata["room_name"] == name &&
        evdata["room_type"] == type)
    {
        //an input is deleted from this room
        map<string, IOBase *>::const_iterator it = model->getCacheInputs().find(evdata["input_id_deleted"]);
        if (it != model->getCacheInputs().end())
        {
            IOBase *io = it->second;
            io->room = NULL;
            ios.erase(find(ios.begin(), ios.end(), io));
            ios.sort(IOHitsCompare);
            updateVisibleIO();
        }
    }

    if (evdata.Exists("output_id_deleted") &&
        evdata["room_name"] == name &&
        evdata["room_type"] == type)
    {
        //an output is deleted from this room
        map<string, IOBase *>::const_iterator it = model->getCacheOutputs().find(evdata["output_id_deleted"]);
        if (it != model->getCacheOutputs().end())
        {
            IOBase *io = it->second;
            io->room = NULL;
            ios.erase(find(ios.begin(), ios.end(), io));
            ios.sort(IOHitsCompare);
            updateVisibleIO();
        }
    }

    if (evdata["old_room_name"] == name &&
        evdata["room_type"] == type)
    {
        name = evdata["new_room_name"];
        room_changed.emit();
    }
    else if (evdata["old_room_type"] == type &&
             evdata["room_name"] == name)
    {
        type = evdata["new_room_type"];
        room_changed.emit();
    }
    else if (evdata["room_type"] == type &&
             evdata["room_name"] == name &&
             evdata["old_room_hits"] == Utils::to_string(hits))
    {
        from_string(evdata["new_room_hits"], hits);

        room_changed.emit();

        model->notifyRoomChange(this);
    }
}

void RoomModel::notifyRoomChange(Room *room)
{
    rooms.sort(RoomHitsCompare);
    updateRoomType();
}

void Room::notifyIOAdd(const string &msgtype, const Params &evdata)
{
    json_t *jreq = nullptr;

    if (evdata["room_name"] != name ||
        evdata["room_type"] != type)
        return; //not for us

    if (msgtype == "input_added")
    {
        json_t *jarr = json_array();
        json_array_append(jarr, json_string(evdata["id"].c_str()));
        jreq = json_pack("{s:o}", "inputs", jarr);

        connection->sendCommand("get_io", jreq, [=](json_t *jres, void *)
        {
            json_t *jin = json_object_get(jres, "inputs");
            if (jin && json_is_object(jin))
            {
                json_t *jio = json_object_get(jin, evdata["id"].c_str());
                if (jio && json_is_object(jio))
                {
                    Params pio;
                    jansson_decode_object(jio, pio);
                    loadNewIOFromNotif(pio, IOBase::IO_INPUT);
                }
            }
        });
    }
    else if (msgtype == "output_added")
    {
        json_t *jarr = json_array();
        json_array_append(jarr, json_string(evdata["id"].c_str()));
        jreq = json_pack("{s:o}", "outputs", jarr);

        connection->sendCommand("get_io", jreq, [=](json_t *jres, void *)
        {
            json_t *jin = json_object_get(jres, "outputs");
            if (jin && json_is_object(jin))
            {
                json_t *jio = json_object_get(jin, evdata["id"].c_str());
                if (jio && json_is_object(jio))
                {
                    Params pio;
                    jansson_decode_object(jio, pio);
                    loadNewIOFromNotif(pio, IOBase::IO_OUTPUT);
                }
            }
        });
    }
}

void Room::loadNewIOFromNotif(const Params &ioparam, int io_type)
{
    if (ioparam["id"] == "" ||
        ioparam["type"] == "" ||
        ioparam["gui_type"] == "")
        return; //don't load wrong IO

    IOBase *io = new IOBase(connection, this, io_type);
    ios.push_back(io);

    io->params = ioparam;

    load_io_done(io);

    string _type = io->params["gui_type"];
    if (_type == "scenario" && io->io_type == IOBase::IO_OUTPUT)
    {
        //Sort cached scenarios again
        model->cacheScenariosPref = model->cacheScenarios;
        model->cacheScenariosPref.sort(IOScenarioCompare);
    }

    ios.sort(IOHitsCompare);

    updateVisibleIO();

    io_added.emit(io);
}

void Room::notifyIODel(const string &msgtype, const Params &evdata)
{
    if (evdata["room_name"] != name ||
        evdata["room_type"] != type)
        return; //not for us

    list<IOBase *>::iterator it;
    for (it = ios.begin(); it != ios.end();it++)
    {
        IOBase *io = (*it);

        if (io->params["id"] == evdata["id"])
        {
            //Remove from cache
            if (msgtype == "input_deleted" && io->io_type == IOBase::IO_INPUT)
            {
                model->cacheInputs.erase(model->cacheInputs.find(io->params["id"]));
            }
            else if (msgtype == "output_deleted" && io->io_type == IOBase::IO_OUTPUT)
            {
                model->cacheOutputs.erase(model->cacheOutputs.find(io->params["id"]));
                if (io->params["gui_type"] == "scenario")
                {
                    model->cacheScenarios.erase(find(model->cacheScenarios.begin(), model->cacheScenarios.end(), io));
                    //Sort cached scenarios again
                    model->cacheScenariosPref = model->cacheScenarios;
                    model->cacheScenariosPref.sort(IOScenarioCompare);
                }
            }
            else
            {
                continue;
            }

            list<IOBase *>::iterator cit;
            for (cit = model->chauffageList.begin();cit != model->chauffageList.end();cit++)
            {
                if (*cit == io)
                {
                    model->chauffageList.erase(cit);
                    break;
                }
            }

            if (io->params["gui_type"] == "shutter" ||
                io->params["gui_type"] == "shutter_smart")
            {
                RoomIOCache::iterator sit = model->cacheShuttersUp.find(io);
                if (sit != model->cacheShuttersUp.end())
                    model->cacheShuttersUp.erase(sit);
            }

            if (io->params["gui_type"] == "light" ||
                io->params["gui_type"] == "light_dimmer" ||
                io->params["gui_type"] == "light_rgb")
            {
                RoomIOCache::iterator sit = model->cacheShuttersUp.find(io);
                if (sit != model->cacheShuttersUp.end())
                    model->cacheShuttersUp.erase(sit);
            }

            io_deleted.emit(io);

            delete io;
            ios.erase(it);

            updateVisibleIO();

            break;
        }
    }
}

void RoomModel::notifyRoomAdd(const string &msgtype, const Params &evdata)
{
    VAR_UNUSED(msgtype);

    Room *room = new Room(connection, this);
    room->type = evdata["type"];
    room->name = evdata["name"];
    Utils::from_string(evdata["hits"], room->hits);

    rooms.push_back(room);
    rooms.sort(RoomHitsCompare);
    updateRoomType();

    room_added.emit(room);
}

void RoomModel::notifyRoomDel(const string &msgtype, const Params &evdata)
{
    VAR_UNUSED(msgtype);

    for (auto it = rooms.begin(); it != rooms.end();it++)
    {
        Room *room = *it;

        if (evdata["type"] == room->type &&
            evdata["name"] == room->name)
        {
            room_deleted.emit(room);
            rooms.erase(it);

            delete room;

            break;
        }
    }
}

list<Room *> RoomModel::getRoomsForType(string type)
{
    list<Room *> rtypes;

    list<Room *>::iterator it, itr;
    for (it = rooms.begin(); it != rooms.end();it++)
    {
        Room *room = *it;

        if (room->type != type)
            continue;

        rtypes.push_back(room);
    }

    rtypes.sort(RoomHitsCompare);

    return rtypes;
}

IOBase *RoomModel::getConsigneFromTemp(IOBase *temp)
{
    if (!temp || temp->params["gui_type"] != "temp" || temp->params["chauffage_id"] == "")
        return NULL;

    list<IOBase *>::iterator it;
    for (it = chauffageList.begin();it != chauffageList.end();it++)
    {
        IOBase *io = (*it);

        if (io->params["gui_type"] == "var_int" &&
            io->params["chauffage_id"] == temp->params["chauffage_id"])
            return io;
    }

    return NULL;
}

IOBase *RoomModel::getTempFromConsigne(IOBase *consigne)
{
    if (!consigne || consigne->params["gui_type"] != "var_int" || consigne->params["chauffage_id"] == "")
        return NULL;

    list<IOBase *>::iterator it;
    for (it = chauffageList.begin();it != chauffageList.end();it++)
    {
        IOBase *io = (*it);

        if (io->params["gui_type"] == "var_int" &&
            io->params["chauffage_id"] == consigne->params["chauffage_id"])
            return io;
    }

    return NULL;
}

map<Room *, list<IOBase *> > RoomModel::getLightsOnForRooms()
{
    map<Room *, list<IOBase *> > lmap;

    RoomIOCache::iterator it = cacheLightsOn.begin();
    for (;it != cacheLightsOn.end();it++)
    {
        RoomIO &rio = (*it).second;

        if (lmap.find(rio.room) == lmap.end())
        {
            list<IOBase *> l;
            l.push_back(rio.io);
            lmap[rio.room] = l;
        }
        else
        {
            list<IOBase *> &l = lmap[rio.room];
            l.push_back(rio.io);
        }
    }

    return lmap;
}

map<Room *, list<IOBase *> > RoomModel::getShuttersUpForRooms()
{
    map<Room *, list<IOBase *> > lmap;

    RoomIOCache::iterator it = cacheShuttersUp.begin();
    for (;it != cacheShuttersUp.end();it++)
    {
        RoomIO &rio = (*it).second;

        if (lmap.find(rio.room) == lmap.end())
        {
            list<IOBase *> l;
            l.push_back(rio.io);
            lmap[rio.room] = l;
        }
        else
        {
            list<IOBase *> &l = lmap[rio.room];
            l.push_back(rio.io);
        }
    }

    return lmap;
}

string IOBase::getIconForIO()
{
    if (params["gui_type"] == "light" ||
        params["gui_type"] == "light_dimmer" ||
        params["gui_type"] == "light_rgb")
        return "calaos/icons/element/simple/light";
    else if (params["gui_type"] == "scenario")
        return "calaos/icons/element/simple/scenario";
    else if (params["gui_type"] == "var_bool")
        return "calaos/icons/element/simple/internal_bool";
    else if (params["gui_type"] == "var_int")
        return "calaos/icons/element/simple/internal_int";
    else if (params["gui_type"] == "var_string" || params["gui_type"] == "string_out" || params["gui_type"] == "string_in")
        return "calaos/icons/element/simple/internal_string";
    else if (params["gui_type"] == "shutter" ||
             params["gui_type"] == "shutter_smart")
        return "calaos/icons/element/simple/volet";
    else if (params["gui_type"] == "analog_in" ||
             params["gui_type"] == "analog_out")
        return "calaos/icons/element/simple/analog";
    else if (params["gui_type"] == "temp")
        return "calaos/icons/element/simple/temp";
    else if (params["gui_type"] == "camera_output")
        return "calaos/icons/element/simple/camera";
    else if (params["gui_type"] == "audio_output")
        return "calaos/icons/element/simple/music";
    else if (params["gui_type"] == "avreceiver")
        return "calaos/icons/element/simple/music";

    return "";
}

void IOBase::getRGBValueFromState(int &r, int &g, int &b)
{
    int val = 0;
    string state = params["state"];

    if (Utils::is_of_type<int>(state))
    {
        from_string(state, val);
    }

    r = ((val >> 16) * 100) / 255;
    g = (((val >> 8) & 0x0000FF) * 100) / 255;
    b = ((val & 0x0000FF) * 100) / 255;
}

int IOBase::computeStateFromRGBValue(int r, int g, int b)
{
    int val = (((int)(r * 255 / 100)) << 16) +
              (((int)(g * 255 / 100)) << 8) +
              ((int)(b * 255 / 100));

    return val;
}

double IOBase::getDaliValueFromState()
{
    double val = 0.0;
    string state = params["state"];

    if (Utils::is_of_type<double>(state))
    {
        from_string(state, val);
    }
    else if (state.compare(0, 4, "set ") == 0)
    {
        string s = state;
        s.erase(0, 4);
        from_string(s, val);
    }
    else if (state == "true")
    {
        val = 100.0;
    }
    else if (state == "false")
    {
        val = 0.0;
    }

    return val;
}

int IOBase::getPercentVoletSmart()
{
    vector<string> tokens;
    int percent;

    Utils::split(params["state"], tokens);

    if (tokens.size() == 2)
        from_string(tokens[1], percent);
    else
        percent = 0;

    return percent;
}

string IOBase::getStatusVoletSmart()
{
    vector<string> tokens;
    Utils::split(params["state"], tokens);

    if (tokens.size() > 0)
        return tokens[0];

    return "";
}

vector<IOActionList> IOBase::getActionList()
{
    vector<IOActionList> v;

    if (params["gui_type"] == "light")
    {
        v.push_back(IOActionList("true", _("Switch light on"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("false", _("Switch light off"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("toggle", _("Toggle light state"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("impulse loop %1 %1 %1 %1", _("Blink the light"), IOActionList::ACTION_TIME_MS));
        v.push_back(IOActionList("impulse %1", _("Switch light on for X seconds"), _("Switch light on for %1"), IOActionList::ACTION_TIME_MS));
    }
    else if (params["gui_type"] == "light_dimmer")
    {
        v.push_back(IOActionList("true", _("Switch light on"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("false", _("Switch light off"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("toggle", _("Toggle light state"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("set %1", _("Dim the light to X percent"), _("Dim the light to %1%"), IOActionList::ACTION_SLIDER));
        v.push_back(IOActionList("set off %1", _("Dim the light to X percent without switching on"), _("Dim the light to %1% without switching on"), IOActionList::ACTION_SLIDER));
        v.push_back(IOActionList("up %1", _("Brighten light for X percent"), _("Brighten light for %1%"), IOActionList::ACTION_NUMBER));
        v.push_back(IOActionList("down %1", _("Dim light for X percent"), _("Dim light for %1%"), IOActionList::ACTION_NUMBER));
        v.push_back(IOActionList("impulse loop %1 %1 %1 %1", _("Blink the light"), IOActionList::ACTION_TIME_MS));
        v.push_back(IOActionList("impulse %1", _("Switch light on for X seconds"), _("Switch light on for %1"), IOActionList::ACTION_TIME_MS));
    }
    else if (params["gui_type"] == "light_rgb")
    {
        v.push_back(IOActionList("true", _("Switch light on"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("false", _("Switch light off"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("toggle", _("Toggle light state"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("set %1", _("Choose color"), _("Set color"), IOActionList::ACTION_COLOR));
        v.push_back(IOActionList("set_red %1", _("Choose red brightness"), _("Set red to %1%"), IOActionList::ACTION_SLIDER));
        v.push_back(IOActionList("up_red %1", _("Increase red for X percent"), _("Increase red for %1%"), IOActionList::ACTION_NUMBER));
        v.push_back(IOActionList("down_red %1", _("Decrease red for X percent"), _("Decrease red for %1%"), IOActionList::ACTION_NUMBER));
        v.push_back(IOActionList("set_green %1", _("Choose green brightness"), _("Set green to %1%"), IOActionList::ACTION_SLIDER));
        v.push_back(IOActionList("up_green %1", _("Increase green for X percent"), _("Increase green for %1%"), IOActionList::ACTION_NUMBER));
        v.push_back(IOActionList("down_green %1", _("Decrease green for X percent"), _("Decrease green for %1%"), IOActionList::ACTION_NUMBER));
        v.push_back(IOActionList("set_blue %1", _("Choose blue brightness"), _("Set blue to %1%"), IOActionList::ACTION_SLIDER));
        v.push_back(IOActionList("up_blue %1", _("Increase blue for X percent"), _("Increase blue for %1%"), IOActionList::ACTION_NUMBER));
        v.push_back(IOActionList("down_blue %1", _("Decrease blue for X percent"), _("Decrease blue for %1%"), IOActionList::ACTION_NUMBER));
    }
    else if (params["gui_type"] == "shutter")
    {
        v.push_back(IOActionList("up", _("Open shutter"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("down", _("Close shutter"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("stop", _("Stop shutter"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("toggle", _("Toggle shutter state"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("impulse up %1", _("Impulse up"), _("Open shutter for %1"), IOActionList::ACTION_TIME_MS));
        v.push_back(IOActionList("impulse down %1", _("Impulse down"), _("Close shutter for %1"), IOActionList::ACTION_TIME_MS));
    }
    else if (params["gui_type"] == "shutter_smart")
    {
        v.push_back(IOActionList("up", _("Open shutter"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("down", _("Close shutter"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("stop", _("Stop shutter"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("toggle", _("Toggle shutter state"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("set %1", _("Set shutter to a position"), _("Set shutter to %1%"), IOActionList::ACTION_SLIDER));
        v.push_back(IOActionList("up %1", _("Open shutter for X percent"), _("Open shutter for %1%"), IOActionList::ACTION_NUMBER));
        v.push_back(IOActionList("down %1", _("Close shutter for X percent"), _("Close shutter for %1%"), IOActionList::ACTION_NUMBER));
        v.push_back(IOActionList("calibrate", _("Start shutter calibration"), IOActionList::ACTION_SIMPLE));
    }
    else if (params["gui_type"] == "analog_out")
    {
        v.push_back(IOActionList("%1", _("Set value"), _("Set value to %1"), IOActionList::ACTION_NUMBER));
    }
    else if (params["gui_type"] == "scenario")
    {
        v.push_back(IOActionList("true", _("Execute scenario"), IOActionList::ACTION_SIMPLE));
        if (params["auto_scenario"] != "")
            v.push_back(IOActionList("false", _("Stop scenario"), IOActionList::ACTION_SIMPLE));
    }
    else if (params["gui_type"] == "var_string" || params["gui_type"] == "string_out")
    {
        v.push_back(IOActionList("%1", _("Set text"), _("Set text to '%1'"), IOActionList::ACTION_TEXT));
    }
    else if (params["gui_type"] == "var_int")
    {
        v.push_back(IOActionList("%1", _("Set value"), _("Set value to %1"), IOActionList::ACTION_NUMBER));
    }
    else if (params["gui_type"] == "var_bool")
    {
        v.push_back(IOActionList("true", _("Activate"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("false", _("Deactivate"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("toggle", _("Toggle state"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("impulse loop %1 %1 %1 %1", _("Blink state"), IOActionList::ACTION_TIME_MS));
        v.push_back(IOActionList("impulse %1", _("Activate for X seconds"), _("Activate for %1"), IOActionList::ACTION_TIME_MS));
    }
    else if (params["gui_type"] == "camera_output")
    {
        v.push_back(IOActionList("recall %1", _("Move camera to a saved position"), _("Move camera to position %1"), IOActionList::ACTION_NUMBER));
        v.push_back(IOActionList("save %1", _("Save position"), _("Save to position %1"), IOActionList::ACTION_NUMBER));
        v.push_back(IOActionList("move up", _("Move up"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("move down", _("Move down"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("move left", _("Move left"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("move right", _("Move right"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("move home", _("Move to default"), IOActionList::ACTION_SIMPLE));
    }
    else if (params["gui_type"] == "audio_output")
    {
        v.push_back(IOActionList("play", _("Play"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("pause", _("Pause"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("stop", _("Stop playing"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("next", _("Next track"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("previous", _("Previous track"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("volume set %1", _("Change volume"), _("Set volume to %1%"), IOActionList::ACTION_SLIDER));
        v.push_back(IOActionList("volume up %1", _("Increase volume for X percent"), _("Increase volume for %1%"), IOActionList::ACTION_NUMBER));
        v.push_back(IOActionList("volume down %1", _("Decrease volume for X percent"), _("Decrease volume for %1%"), IOActionList::ACTION_NUMBER));
        v.push_back(IOActionList("power on", _("Power on music zone"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("power off", _("Power off music zone"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("sleep %1", _("Power off music zone in X seconds (Sleep mode)"), _("Power off music zone in %1% seconds (sleep mode)"), IOActionList::ACTION_NUMBER));
        //TODO: playlist/song selection and playing here
    }
    else if (params["gui_type"] == "avreceiver")
    {
        v.push_back(IOActionList("power on", _("Power On"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("power off", _("Power Off"), IOActionList::ACTION_SIMPLE));
        v.push_back(IOActionList("volume %1", _("Change volume"), _("Change volume to %1%"), IOActionList::ACTION_SLIDER));

        cout << params.toString() << endl;

        map<int, string>::iterator it = amplifier_inputs.begin();

        for (;it != amplifier_inputs.end();it++)
        {
            int source_id = (*it).first;
            string input_name = (*it).second;

            string src_action = "source " + Utils::to_string(source_id);
            string src_actionname = _("Switch source to ") + Utils::to_string(input_name);

            v.push_back(IOActionList(src_action, src_actionname, IOActionList::ACTION_SIMPLE));
        }
    }

    return v;
}

IOActionList IOBase::getActionFromState()
{
    IOActionList ac;

    if (params["gui_type"] == "light")
    {
        if (params["state"] == "true")
            ac = IOActionList("true", _("Switch light on"), IOActionList::ACTION_SIMPLE);
        else
            ac = IOActionList("false", _("Switch light off"), IOActionList::ACTION_SIMPLE);
    }
    else if (params["gui_type"] == "light_dimmer")
    {
        if (params["state"] == "true")
            ac = IOActionList("true", _("Switch light on"), IOActionList::ACTION_SIMPLE);
        else if (params["state"] == "false")
            ac = IOActionList("false", _("Switch light off"), IOActionList::ACTION_SIMPLE);
        else
            ac = IOActionList("set %1", _("Set light to X percent"), _("Set light to %1%"), IOActionList::ACTION_SLIDER);

        ac.dvalue = getDaliValueFromState();
    }
    else if (params["gui_type"] == "light_rgb")
    {
        if (params["state"] == "true")
            ac = IOActionList("true", _("Switch light on"), IOActionList::ACTION_SIMPLE);
        else if (params["state"] == "false")
            ac = IOActionList("false", _("Switch light off"), IOActionList::ACTION_SIMPLE);
        else
            ac = IOActionList("set %1", _("Choose color"), _("Set color"), IOActionList::ACTION_COLOR);

        getRGBValueFromState(ac.red, ac.green, ac.blue);
    }
    else if (params["gui_type"] == "shutter")
    {
        ac = IOActionList("up", _("Open shutter"), IOActionList::ACTION_SIMPLE);
    }
    else if (params["gui_type"] == "shutter_smart")
    {
        ac = IOActionList("up", _("Open shutter"), IOActionList::ACTION_SIMPLE);
    }
    else if (params["gui_type"] == "analog_out")
    {
        ac = IOActionList("%1", _("Set value"), _("Set value to %1"), IOActionList::ACTION_NUMBER);
        ac.dvalue = 0.0;
    }
    else if (params["gui_type"] == "scenario")
    {
        ac = IOActionList("true", _("Execute scenario"), IOActionList::ACTION_SIMPLE);
    }
    else if (params["gui_type"] == "var_string" || params["gui_type"] == "string_out")
    {
        ac = IOActionList("%1", _("Set text"), _("Set text to '%1'"), IOActionList::ACTION_TEXT);
        ac.svalue = "Un Texte";
    }
    else if (params["gui_type"] == "var_int")
    {
        ac = IOActionList("%1", _("Set value"), _("Set value to %1"), IOActionList::ACTION_NUMBER);
        ac.dvalue = 0.0;
    }
    else if (params["gui_type"] == "var_bool")
    {
        ac = IOActionList("true", _("Activate"), IOActionList::ACTION_SIMPLE);
    }
    else if (params["gui_type"] == "camera_output")
    {
        ac = IOActionList("recall %1", _("Move camera to a saved position"), _("Move camera to position %1"), IOActionList::ACTION_NUMBER);
        ac.dvalue = 0.0;
    }
    else if (params["gui_type"] == "audio_output")
    {
        ac = IOActionList("play", _("Play"), IOActionList::ACTION_SIMPLE);
    }
    else if (params["gui_type"] == "avreceiver")
    {
        ac = IOActionList("power on", _("Power On"), IOActionList::ACTION_SIMPLE);
    }

    return ac;
}

IOActionList IOBase::getActionListFromAction(string action)
{
    IOActionList ac;
    vector<string> tokens;

    split(action, tokens);
    if (tokens.size() < 1) return ac;

    if (params["gui_type"] == "light")
    {
        if (tokens[0] == "true") ac = IOActionList("true", _("Switch light on"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "false") ac = IOActionList("false", _("Switch light off"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "toggle") ac = IOActionList("toggle", _("Toggle light state"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "impulse" && tokens[1] == "loop") ac = IOActionList("impulse loop %1 %1 %1 %1", _("Blink the light"), IOActionList::ACTION_TIME_MS);
        else if (tokens[0] == "impulse") ac = IOActionList("impulse %1", _("Switch light on for X seconds"), _("Switch light on for %1"), IOActionList::ACTION_TIME_MS);

        if (tokens.size() > 1)
            from_string(tokens[tokens.size() - 1], ac.dvalue);
    }
    else if (params["gui_type"] == "light_dimmer")
    {
        if (tokens[0] == "true") ac = IOActionList("true", _("Switch light on"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "false") ac = IOActionList("false", _("Switch light off"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "toggle") ac = IOActionList("toggle", _("Toggle light state"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "set" && tokens[1] == "off") ac = IOActionList("set off %1", _("Dim the light to X percent without switching on"), _("Dim the light to %1% without switching on"), IOActionList::ACTION_SLIDER);
        else if (tokens[0] == "set") ac = IOActionList("set %1", _("Dim the light to X percent"), _("Dim the light to %1%"), IOActionList::ACTION_SLIDER);
        else if (tokens[0] == "up") ac = IOActionList("up %1", _("Brighten light for X percent"), _("Brighten light for %1%"), IOActionList::ACTION_NUMBER);
        else if (tokens[0] == "down") ac = IOActionList("down %1", _("Dim light for X percent"), _("Dim light for %1%"), IOActionList::ACTION_NUMBER);
        else if (tokens[0] == "impulse" && tokens[1] == "loop") ac = IOActionList("impulse loop %1 %1 %1 %1", _("Blink the light"), IOActionList::ACTION_TIME_MS);
        else if (tokens[0] == "impulse") ac = IOActionList("impulse %1", _("Switch light on for X seconds"), _("Switch light on for %1"), IOActionList::ACTION_TIME_MS);

        if (tokens.size() > 1)
            from_string(tokens[tokens.size() - 1], ac.dvalue);
    }
    else if (params["gui_type"] == "light_rgb")
    {
        if (tokens[0] == "true") ac = IOActionList("true", _("Switch light on"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "false") ac = IOActionList("false", _("Switch light off"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "toggle") ac = IOActionList("toggle", _("Toggle light state"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "set") ac = IOActionList("set %1", _("Choose color"), _("Set color"), IOActionList::ACTION_COLOR);
        else if (tokens[0] == "set_red") ac = IOActionList("set_red %1", _("Choose red brightness"), _("Set red to %1%"), IOActionList::ACTION_SLIDER);
        else if (tokens[0] == "up_red") ac = IOActionList("up_red %1", _("Increase red for X percent"), _("Increase red for %1%"), IOActionList::ACTION_NUMBER);
        else if (tokens[0] == "down_red") ac = IOActionList("down_red %1", _("Decrease red for X percent"), _("Decrease red for %1%"), IOActionList::ACTION_NUMBER);
        else if (tokens[0] == "set_green") ac = IOActionList("set_green %1", _("Choose green brightness"), _("Set green to %1%"), IOActionList::ACTION_SLIDER);
        else if (tokens[0] == "up_green") ac = IOActionList("up_green %1", _("Increase green for X percent"), _("Increase green for %1%"), IOActionList::ACTION_NUMBER);
        else if (tokens[0] == "down_green") ac = IOActionList("down_green %1", _("Decrease green for X percent"), _("Decrease green for %1%"), IOActionList::ACTION_NUMBER);
        else if (tokens[0] == "set_blue") ac = IOActionList("set_blue %1", _("Choose blue brightness"), _("Set blue to %1%"), IOActionList::ACTION_SLIDER);
        else if (tokens[0] == "up_blue") ac = IOActionList("up_blue %1", _("Increase blue for X percent"), _("Increase blue for %1%"), IOActionList::ACTION_NUMBER);
        else if (tokens[0] == "down_blue") ac = IOActionList("down_blue %1", _("Decrease blue for X percent"), _("Decrease blue for %1%"), IOActionList::ACTION_NUMBER);

        if (tokens.size() > 1)
            from_string(tokens[tokens.size() - 1], ac.dvalue);
    }
    else if (params["gui_type"] == "shutter")
    {
        if (tokens[0] == "up") ac = IOActionList("up", _("Open shutter"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "down") ac = IOActionList("down", _("Close shutter"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "stop") ac = IOActionList("stop", _("Stop shutter"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "toggle") ac = IOActionList("toggle", _("Toggle shutter state"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "impulse" && tokens[1] == "up") ac = IOActionList("impulse up %1", _("Impulse up"), _("Open shutter for %1"), IOActionList::ACTION_TIME_MS);
        else if (tokens[0] == "impulse" && tokens[1] == "down") ac = IOActionList("impulse down %1", _("Impulse down"), _("Close shutter for %1"), IOActionList::ACTION_TIME_MS);

        if (tokens.size() > 1)
            from_string(tokens[tokens.size() - 1], ac.dvalue);
    }
    else if (params["gui_type"] == "shutter_smart")
    {
        if (tokens[0] == "up") ac = IOActionList("up", _("Open shutter"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "down") ac = IOActionList("down", _("Close shutter"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "stop") ac = IOActionList("stop", _("Stop shutter"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "toggle") ac = IOActionList("toggle", _("Toggle shutter state"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "set") ac = IOActionList("set %1", _("Set shutter to a position"), _("Set shutter to %1%"), IOActionList::ACTION_SLIDER);
        else if (tokens[0] == "up") ac = IOActionList("up %1", _("Open shutter for X percent"), _("Open shutter for %1%"), IOActionList::ACTION_NUMBER);
        else if (tokens[0] == "down") ac = IOActionList("down %1", _("Close shutter for X percent"), _("Close shutter for %1%"), IOActionList::ACTION_NUMBER);
        else if (tokens[0] == "calibrate") ac = IOActionList("calibrate", _("Start shutter calibration"), IOActionList::ACTION_SIMPLE);

        if (tokens.size() > 1)
            from_string(tokens[tokens.size() - 1], ac.dvalue);
    }
    else if (params["gui_type"] == "analog_out")
    {
        ac = IOActionList("%1", "Mettre une valeur", _("Set value to %1"), IOActionList::ACTION_NUMBER);
        from_string(tokens[0], ac.dvalue);
    }
    else if (params["gui_type"] == "scenario")
    {
        if (tokens[0] == "true") ac = IOActionList("true", _("Execute scenario"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "false") ac = IOActionList("false", _("Stop scenario"), IOActionList::ACTION_SIMPLE);
    }
    else if (params["gui_type"] == "var_string" || params["gui_type"] == "var_string")
    {
        ac = IOActionList("%1", _("Set text"), _("Set text to '%1'"), IOActionList::ACTION_TEXT);
        ac.svalue = action;
    }
    else if (params["gui_type"] == "var_int")
    {
        ac = IOActionList("%1", _("Set value"), _("Set value to %1"), IOActionList::ACTION_NUMBER);
        from_string(tokens[0], ac.dvalue);
    }
    else if (params["gui_type"] == "var_bool")
    {
        if (tokens[0] == "true") ac = IOActionList("true", _("Activate"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "false") ac = IOActionList("false", _("Deactivate"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "toggle") ac = IOActionList("toggle", _("Toggle state"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "impulse" && tokens[1] == "loop") ac = IOActionList("impulse loop %1 %1 %1 %1", _("Blink state"), IOActionList::ACTION_TIME_MS);
        else if (tokens[0] == "impulse") ac = IOActionList("impulse %1", "Activer pendant X secondes", _("Activate for %1"), IOActionList::ACTION_TIME_MS);
    }
    else if (params["gui_type"] == "camera_output")
    {
        if (tokens[0] == "recall")
        {
            ac = IOActionList("recall %1", _("Move camera to a saved position"), _("Move camera to position %1"), IOActionList::ACTION_NUMBER);
            from_string(tokens[tokens.size() - 1], ac.dvalue);
        }
        else if (tokens[0] == "save") ac = IOActionList("save %1", _("Save position"), _("Save to position %1"), IOActionList::ACTION_NUMBER);
        else if (tokens[0] == "move")
        {
            if (tokens[1] == "up") ac = IOActionList("move up", _("Move up"), IOActionList::ACTION_SIMPLE);
            else if (tokens[1] == "down") ac = IOActionList("move up", _("Move down"), IOActionList::ACTION_SIMPLE);
            else if (tokens[1] == "left") ac = IOActionList("move left", _("Move left"), IOActionList::ACTION_SIMPLE);
            else if (tokens[1] == "right") ac = IOActionList("move right", _("Move right"), IOActionList::ACTION_SIMPLE);
            else if (tokens[1] == "home") ac = IOActionList("move home", _("Move to default"), IOActionList::ACTION_SIMPLE);
        }
    }
    else if (params["gui_type"] == "audio_output")
    {
        if (tokens[0] == "play") ac = IOActionList("play", _("Play"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "pause") ac = IOActionList("pause", _("Plause"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "stop") ac = IOActionList("stop", _("Stop playing"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "next") ac = IOActionList("next", _("Next track"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "previous") ac = IOActionList("previous", _("Previous track"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "volume")
        {
            if (tokens[1] == "set") ac = IOActionList("volume set %1", _("Change volume"), _("Set volume to %1%"), IOActionList::ACTION_SLIDER);
            else if (tokens[1] == "up") ac = IOActionList("volume up %1", _("Increase volume for X percent"), _("Increase volume for %1%"), IOActionList::ACTION_NUMBER);
            else if (tokens[1] == "down") ac = IOActionList("volume down %1", _("Decrease volume for X percent"), _("Decrease volume for %1%"), IOActionList::ACTION_NUMBER);
            from_string(tokens[tokens.size() - 1], ac.dvalue);
        }
        else if (tokens[0] == "power" && tokens[1] == "on") ac = IOActionList("power on", _("Power on music zone"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "power" && tokens[1] == "off") ac = IOActionList("power off", _("Power off music zone"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "sleep")
        {
            ac = IOActionList("sleep %1", _("Power off music zone in X seconds (Sleep mode)"), _("Power off music zone in %1% seconds (sleep mode)"), IOActionList::ACTION_NUMBER);
            from_string(tokens[tokens.size() - 1], ac.dvalue);
        }
        //TODO: playlist/song selection and playing here
    }
    else if (params["gui_type"] == "avreceiver")
    {
        if (tokens[0] == "power on") ac = IOActionList("power on", _("Power On"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "power off") ac = IOActionList("power off", _("Power Off"), IOActionList::ACTION_SIMPLE);
        else if (tokens[0] == "volume")
        {
            ac = IOActionList("volume %1", _("Change volume"), _("Change volume to %1%"), IOActionList::ACTION_SLIDER);
            from_string(tokens[tokens.size() - 1], ac.dvalue);
        }
        else if (tokens[0] == "source")
        {
            int inputid;
            from_string(tokens[tokens.size() - 1], inputid);

            map<int, string>::iterator it = amplifier_inputs.begin();

            for (;it != amplifier_inputs.end();it++)
            {
                int source_id = (*it).first;
                string input_name = (*it).second;

                string src_action = "source " + Utils::to_string(source_id);
                string src_actionname = _("Switch source to ") + Utils::to_string(input_name);

                if (inputid == source_id)
                {
                    ac = IOActionList(src_action, src_actionname, IOActionList::ACTION_SIMPLE);
                    break;
                }
            }
        }
    }

    return ac;
}

string IOActionList::getComputedTitle(IOBase *io)
{
    string t = title_computed;

    if (type == ACTION_SLIDER ||
        type == ACTION_NUMBER)
        Utils::replace_str(t, "%1", Utils::to_string(dvalue));

    if (type == ACTION_TEXT)
        Utils::replace_str(t, "%1", Utils::to_string(svalue));

    if (type == ACTION_TIME_MS)
        Utils::replace_str(t, "%1", Utils::time2string(dvalue / 1000, (long)dvalue % 1000));

    return t;
}

string IOActionList::getComputedAction(IOBase *io)
{
    string ac = action;

    if (type == ACTION_SIMPLE)
        return ac;

    if (type == ACTION_SLIDER ||
        type == ACTION_NUMBER ||
        type == ACTION_TIME_MS)
        Utils::replace_str(ac, "%1", Utils::to_string(dvalue));

    if (type == ACTION_TEXT)
        Utils::replace_str(ac, "%1", Utils::to_string(svalue));

    if (type == ACTION_COLOR)
        Utils::replace_str(ac, "%1", Utils::to_string(io->computeStateFromRGBValue(red, green, blue)));

    return ac;
}

void IOBase::loadPlage()
{
    if (params["gui_type"] != "time_range")
    {
        cErrorDom("network") << params["id"]
                             << " is not of type time_range, but " << params["gui_type"]
                             << " instead.";
        return;
    }

    string cmd = "input " + params["id"] + " plage get";
    cCritical() << "TODO";
    //connection->SendCommand(cmd, sigc::mem_fun(*this, &IOBase::loadPlage_cb));
}

void IOBase::loadPlage_cb(bool success, vector<string> result, void *data)
{
    if (!success) return;

    range_infos.range_monday.clear();
    range_infos.range_tuesday.clear();
    range_infos.range_wednesday.clear();
    range_infos.range_thursday.clear();
    range_infos.range_friday.clear();
    range_infos.range_saturday.clear();
    range_infos.range_sunday.clear();

    for (uint i = 3;i < result.size();i++)
    {
        vector<string> splitter;
        split(result[i], splitter, ":", 11);

        TimeRange tr;

        tr.shour = splitter[1];
        tr.smin = splitter[2];
        tr.ssec = splitter[3];
        from_string(splitter[4], tr.start_type);
        from_string(splitter[5], tr.start_offset);
        if (tr.start_offset < 0) tr.start_offset = -1;
        if (tr.start_offset >= 0) tr.start_offset = 1;

        tr.ehour = splitter[6];
        tr.emin = splitter[7];
        tr.esec = splitter[8];
        from_string(splitter[9], tr.end_type);
        from_string(splitter[10], tr.end_offset);
        if (tr.end_offset < 0) tr.end_offset = -1;
        if (tr.end_offset >= 0) tr.end_offset = 1;

        int day;
        from_string(splitter[0], day);
        switch(day)
        {
        case 1: range_infos.range_monday.push_back(tr); break;
        case 2: range_infos.range_tuesday.push_back(tr); break;
        case 3: range_infos.range_wednesday.push_back(tr); break;
        case 4: range_infos.range_thursday.push_back(tr); break;
        case 5: range_infos.range_friday.push_back(tr); break;
        case 6: range_infos.range_saturday.push_back(tr); break;
        case 7: range_infos.range_sunday.push_back(tr); break;
        default:
            cErrorDom("network") << params["id"]
                                 << " unknown range day. Debug infos: " << result[i];
            break;
        }
    }

    //Load months info
    string cmd = "input " + params["id"] + " plage months get";
    cCritical() << "TODO";
    //connection->SendCommand(cmd, sigc::mem_fun(*this, &IOBase::loadPlageMonths_cb));
}

void IOBase::loadPlageMonths_cb(bool success, vector<string> result, void *data)
{
    if (!success) return;

    if (result.size() < 5)
    {
        cErrorDom("network") << params["id"]
                             << "error reading months!";
        io_changed.emit(); //io has changed

        return;
    }

    string m = result[4];
    //reverse to have a left to right months representation
    std::reverse(m.begin(), m.end());

    try
    {
        bitset<12> mset(m);
        range_infos.range_months = mset;
    }
    catch(...)
    {
        cErrorDom("network") << params["id"]
                             << " wrong parameters for months: " << m;
    }

    cDebugDom("network") << params["id"]
                         << " TimeRange reloaded successfully";

    io_changed.emit(); //io has changed
}

vector<int> TimeRangeInfos::isScheduledDate(struct tm scDate)
{
    vector<int> ret;

    cDebugDom("scenario") << "Checking scenario for date ";

    //check month
    if (!range_months.test(scDate.tm_mon))
        return ret;

    auto checkRange = [=,&ret](const vector<TimeRange> &range)
    {
        cDebugDom("scenario") << "day: " << scDate.tm_wday << " range size: " << range.size();
        for (uint i = 0;i < range.size();i++)
        {
            cDebugDom("scenario") << "adding range: " << i;
            ret.push_back(i);
        }
    };

    switch (scDate.tm_wday)
    {
    case 1: checkRange(range_monday); break;
    case 2: checkRange(range_tuesday); break;
    case 3: checkRange(range_wednesday); break;
    case 4: checkRange(range_thursday); break;
    case 5: checkRange(range_friday); break;
    case 6: checkRange(range_saturday); break;
    case 0: checkRange(range_sunday); break;
    default: break;
    }

    cDebugDom("scenario") << "Range size: " << ret.size();
    return ret;
}

vector<TimeRange> TimeRangeInfos::getRange(int day)
{
    switch (day)
    {
    case TimeRange::MONDAY: return range_monday; break;
    case TimeRange::TUESDAY: return range_tuesday; break;
    case TimeRange::WEDNESDAY: return range_wednesday; break;
    case TimeRange::THURSDAY: return range_thursday; break;
    case TimeRange::FRIDAY: return range_friday; break;
    case TimeRange::SATURDAY: return range_saturday; break;
    case TimeRange::SUNDAY: return range_sunday; break;
    default: break;
    }
    vector<TimeRange> v;
    return v;
}

string TimeRangeInfos::toString()
{
    stringstream str;

    str << "*** Monday ***" << endl;
    for (uint i = 0; i < range_monday.size();i++)
        str << "\t" << range_monday[i].toString() << endl;
    str << "*** Tuesday ***" << endl;
    for (uint i = 0; i < range_tuesday.size();i++)
        str << "\t" << range_tuesday[i].toString() << endl;
    str << "*** Wednesday ***" << endl;
    for (uint i = 0; i < range_wednesday.size();i++)
        str << "\t" << range_wednesday[i].toString() << endl;
    str << "*** Thursday ***" << endl;
    for (uint i = 0; i < range_thursday.size();i++)
        str << "\t" << range_thursday[i].toString() << endl;
    str << "*** Friday ***" << endl;
    for (uint i = 0; i < range_friday.size();i++)
        str << "\t" << range_friday[i].toString() << endl;
    str << "*** Saturday ***" << endl;
    for (uint i = 0; i < range_saturday.size();i++)
        str << "\t" << range_saturday[i].toString() << endl;
    str << "*** Sunday ***" << endl;
    for (uint i = 0; i < range_sunday.size();i++)
        str << "\t" << range_sunday[i].toString() << endl;

    return str.str();
}
