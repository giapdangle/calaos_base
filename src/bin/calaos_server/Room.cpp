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
#include "Room.h"
#include "ListeRule.h"
#include "ListeRoom.h"
#include "AVReceiver.h"

using namespace Calaos;

Room::Room(string _name, string _type, int _hits):
    name(_name),
    type(_type),
    hits(_hits)
{
    cDebugDom("room") << "Room::Room(" << name << ", " << type << "): Ok";
}

Room::~Room()
{
    while (inputs.size() > 0)
        ListeRoom::Instance().deleteIO(inputs[0]);

    while (outputs.size() > 0)
        ListeRoom::Instance().deleteIO(outputs[0]);

    inputs.clear();
    outputs.clear();

    cDebugDom("room");
}

void Room::AddInput(Input *in)
{
    inputs.push_back(in);

    cDebugDom("room") << "(" << in->get_param("id") << "): Ok";
}

void Room::AddOutput(Output *out)
{
    outputs.push_back(out);

    cDebugDom("room") << "(" << out->get_param("id") << "): Ok";
}

void Room::RemoveInput(int pos, bool del)
{
    EventManager::create(CalaosEvent::EventInputDeleted,
                         { { "id", inputs[pos]->get_param("id") },
                           { "room_name", get_name() },
                           { "room_type", get_type() } });

    vector<Input *>::iterator iter = inputs.begin();
    for (int i = 0;i < pos;iter++, i++) ;
    if (del) delete inputs[pos];
    inputs.erase(iter);

    cDebugDom("room");
}

void Room::RemoveOutput(int pos, bool del)
{
    EventManager::create(CalaosEvent::EventOutputDeleted,
                         { { "id", outputs[pos]->get_param("id") },
                           { "room_name", get_name() },
                           { "room_type", get_type() } });

    vector<Output *>::iterator iter = outputs.begin();
    for (int i = 0;i < pos;iter++, i++) ;
    if (del) delete outputs[pos];
    outputs.erase(iter);

    cDebugDom("room");
}

void Room::RemoveInputFromRoom(Input *in)
{
    vector<Input *>::iterator it = find(inputs.begin(), inputs.end(), in);
    if (it != inputs.end())
    {
        inputs.erase(it);

        EventManager::create(CalaosEvent::EventRoomChanged,
                             { { "input_id_deleted", in->get_param("id") },
                               { "room_name", get_name() },
                               { "room_type", get_type() } });
    }
}

void Room::RemoveOutputFromRoom(Output *out)
{
    vector<Output *>::iterator it = find(outputs.begin(), outputs.end(), out);
    if (it != outputs.end())
    {
        outputs.erase(it);

        EventManager::create(CalaosEvent::EventRoomChanged,
                             { { "output_id_deleted", out->get_param("id") },
                               { "room_name", get_name() },
                               { "room_type", get_type() } });
    }
}

void Room::set_name(std::string &s)
{
    EventManager::create(CalaosEvent::EventRoomChanged,
                         { { "old_room_name", get_name() },
                           { "new_room_name", s },
                           { "room_type", get_type() } });

    name = s;
}

void Room::set_type(std::string &s)
{
    EventManager::create(CalaosEvent::EventRoomChanged,
                         { { "old_room_type", get_type() },
                           { "new_room_type", s },
                           { "room_name", get_name() } });

    type = s;
}

void Room::set_hits(int h)
{
    EventManager::create(CalaosEvent::EventRoomChanged,
                         { { "old_room_hits", Utils::to_string(hits) },
                           { "new_room_hits", Utils::to_string(h) },
                           { "room_name", get_name() },
                           { "room_type", get_type() } });

    hits = h;
}

bool Room::LoadFromXml(TiXmlElement *room_node)
{
    TiXmlElement *node = room_node->FirstChildElement();
    for(; node; node = node->NextSiblingElement())
    {
        if (node->ValueStr() == "calaos:input")
        {
            Input *in = IOFactory::Instance().CreateInput(node);
            if (in)
            {
                AddInput(in);

                InputTimer *o = dynamic_cast<InputTimer *>(in);
                if (o) AddOutput(o);

                Scenario *sc = dynamic_cast<Scenario *>(in);
                if (sc) AddOutput(sc);
            }
        }
        else if (node->ValueStr() == "calaos:output")
        {
            Output *out = IOFactory::Instance().CreateOutput(node);
            if (out) AddOutput(out);
        }
        else if (node->ValueStr() == "calaos:audio")
        {
            AudioPlayer *player = IOFactory::Instance().CreateAudio(node);
            if (player)
            {
                if (AudioManager::Instance().get_size() <= 0)
                    AudioManager::Instance().Add(player, player->get_param("host"));
                else
                    AudioManager::Instance().Add(player);

                AddInput(player->get_input());
                AddOutput(player->get_output());
            }
        }
        else if (node->ValueStr() == "calaos:internal")
        {
            Input *in = IOFactory::Instance().CreateInput(node);
            if (in)
            {
                Internal *intern = dynamic_cast<Internal *>(in);
                if (intern)
                {
                    AddInput(intern);
                    AddOutput(intern);
                }
            }
        }
        else if (node->ValueStr() == "calaos:camera")
        {
            IPCam *camera = IOFactory::Instance().CreateIPCamera(node);
            if (camera)
            {
                CamManager::Instance().Add(camera);

                AddInput(camera->get_input());
                AddOutput(camera->get_output());
            }
        }
        else if (node->ValueStr() == "calaos:avr")
        {
            Output *o = IOFactory::Instance().CreateOutput(node);
            if (o)
            {
                IOAVReceiver *receiver = dynamic_cast<IOAVReceiver *>(o);
                if (receiver)
                {
                    AddInput(receiver);
                    AddOutput(receiver);
                }
            }
        }
    }

    return true;
}

bool Room::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *room_node = new TiXmlElement("calaos:room");
    room_node->SetAttribute("name", name);
    room_node->SetAttribute("type", type);
    room_node->SetAttribute("hits", Utils::to_string(hits));
    node->LinkEndChild(room_node);

    for (int i = 0;i < get_size_in();i++)
    {
        Input *input = get_input(i);

        if (input->get_param("gui_type") == "switch" || input->get_param("gui_type") == "switch3" ||
            input->get_param("gui_type") == "switch_long" ||
            input->get_param("gui_type") == "temp" ||
            input->get_param("gui_type") == "scenario" ||
            input->get_param("gui_type") == "time" || input->get_param("gui_type") == "timer" ||
            input->get_param("gui_type") == "var_bool" ||
            input->get_param("gui_type") == "var_int" ||
            input->get_param("gui_type") == "var_string" ||
            input->get_param("gui_type") == "time_range" ||
            input->get_param("gui_type") == "analog_in" ||
            input->get_param("gui_type") == "string_in")
        {
            input->SaveToXml(room_node);
        }
    }

    for (int i = 0;i < get_size_out();i++)
    {
        Output *output = get_output(i);

        if (output->get_param("gui_type") == "light" || output->get_param("gui_type") == "analog_out" ||
            output->get_param("gui_type") == "shutter_smart" || output->get_param("gui_type") == "shutter" ||
            output->get_param("gui_type") == "light_dimmer" || output->get_param("gui_type") == "light_rgb" ||
            output->get_param("gui_type") == "avreceiver" || output->get_param("gui_type") == "string_out")
        {
            output->SaveToXml(room_node);
        }

        AudioOutput *audio_output = dynamic_cast<AudioOutput *>(output);
        if (audio_output)
        {
            audio_output->get_player()->SaveToXml(room_node);
        }

        CamOutput *camera_output = dynamic_cast<CamOutput *>(output);
        if (camera_output)
        {
            camera_output->get_cam()->SaveToXml(room_node);
        }
    }

    return true;
}
