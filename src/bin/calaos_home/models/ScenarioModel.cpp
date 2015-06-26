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
#include "ScenarioModel.h"
#include "CalaosModel.h"

ScenarioModel::ScenarioModel(CalaosConnection *con):
    connection(con)
{
    connection->notify_scenario_add.connect(
                sigc::mem_fun(*this, &ScenarioModel::notifyScenarioAdd));
    connection->notify_scenario_change.connect(
                sigc::mem_fun(*this, &ScenarioModel::notifyScenarioChange));
}

ScenarioModel::~ScenarioModel()
{
    for_each(scenarios.begin(), scenarios.end(), Delete());
}

void ScenarioModel::load(json_t *data)
{
    cCritical() << "TODO: load autoscenarios";
    load_done.emit();
}

void ScenarioModel::scenario_list_cb(bool success, vector<string> result, void *data)
{/*
    if (!success) return;

    if (result.size() == 2)
        load_done.emit(); //no scenarios found

    if (result.size() < 3) return;

    load_count = 0;
    for (uint i = 2;i < result.size();i++)
    {
        map<string, IOBase *>::const_iterator it = CalaosModel::Instance().getHome()->getCacheInputs().find(result[i]);

        if (it == CalaosModel::Instance().getHome()->getCacheInputs().end())
        {
            cErrorDom("scenario") << "Unknown Input \'" << result[i] << "\' !";
            continue;
        }

        Scenario *sc = new Scenario(connection);
        sc->ioScenario = it->second;
        sc->scenario_data.empty = false;
        sc->scenario_data.params.Add("id", result[i]);
        sc->ioScenario->io_deleted.connect(sigc::bind(sigc::mem_fun(*this, &ScenarioModel::notifyScenarioDel), sc));
        scenarios.push_back(sc);

        load_count++;
        sc->load_done.connect(sigc::mem_fun(*this, &ScenarioModel::load_scenario_done));

        string cmd = "scenario get " + result[i];
        connection->SendCommand(cmd, [=](bool _success, vector<string> _result, void *_data)
        {
            sc->scenario_get_cb(_success, _result, _data);
            sc->load_done.emit(sc);
        });
    }*/
}

void Scenario::scenario_get_cb(bool success, vector<string> result, void *data)
{
    scenario_data.name = ioScenario->params["name"];
    if (ioScenario->params["visible"] == "true")
    {
        scenario_data.visible = true;
        scenario_data.room = ioScenario->getRoom();
    }

    int step = -1;
    for (uint b = 3;b < result.size();b++)
    {
        vector<string> tmp;
        Utils::split(result[b], tmp, ":", 2);

        if (tmp.size() < 2) continue;

        if (tmp[0] == "step")
        {
            step++;

            ScenarioStep sstep;
            from_string(tmp[1], sstep.pause);
            scenario_data.steps.push_back(sstep);

            continue;
        }

        if (tmp[0] == "step_end")
        {
            step = ScenarioData::END_STEP;

            continue;
        }

        if (step == -1)
        {
            scenario_data.params.Add(tmp[0], tmp[1]);

            if (tmp[0] == "schedule" && tmp[1] != "false")
            {
                map<string, IOBase *>::const_iterator it = CalaosModel::Instance().getHome()->getCacheInputs().find(tmp[1]);
                if (it != CalaosModel::Instance().getHome()->getCacheInputs().end())
                {
                    ioSchedule = (*it).second;
                    ioSchedule->io_deleted.connect([=]()
                    {
                        ioSchedule = nullptr;
                    });
                }
            }
        }
        else
        {
            map<string, IOBase *>::const_iterator it = CalaosModel::Instance().getHome()->getCacheOutputs().find(tmp[0]);
            if (it == CalaosModel::Instance().getHome()->getCacheOutputs().end())
            {
                cErrorDom("scenario") << "Unknown action id \'" << tmp[0] << "\' with action \'" << tmp[1] << "\' !";
                continue;
            }
            IOBase *io = (*it).second;

            IOActionList ac = io->getActionListFromAction(tmp[1]);
            ScenarioAction sa;
            sa.io = io;
            sa.action = ac.getComputedAction(io);

            if (step == ScenarioData::END_STEP)
                scenario_data.step_end.actions.push_back(sa);
            else
                scenario_data.steps[step].actions.push_back(sa);
        }
    }
}

Room *Scenario::getRoom()
{
    if (!room && ioScenario && ioScenario->getRoom())
        room = ioScenario->getRoom();

    return room;
}

string Scenario::getFirstCategory()
{
    vector<string> tok;
    split(scenario_data.params["category"], tok, "-");
    if (tok.size() > 0)
        return tok[0];

    return "other";
}

string ScenarioData::createRequest()
{
    //request:
    // scenario create name:bla visible:true room_name:bla room_type:bla cycle:true step:61000 output_0:actionbla output_1:actionbla step_end output_0:actionbla

    string req = "scenario create name:" + url_encode(name) + " ";

    if (visible && room)
    {
        req += "visible:true room_name:" + url_encode(room->name) + " ";
        req += "room_type:" + url_encode(room->type) + " ";
    }
    else
    {
        req += "visible:false ";
    }

    if (params["cycle"] == "true")
        req += "cycle:true ";
    else
        req += "cycle:false ";

    //Add each steps
    for (uint i = 0;i < steps.size();i++)
    {
        ScenarioStep &step = steps[i];

        req += "step:" + Utils::to_string(step.pause) + " ";

        for (uint j = 0;j < step.actions.size();j++)
        {
            ScenarioAction &sa = step.actions[j];

            req += url_encode(sa.io->params["id"]) + ":" + url_encode(sa.action) + " ";
        }
    }

    //End step
    req += "step_end ";
    for (uint j = 0;j < step_end.actions.size();j++)
    {
        ScenarioAction &sa = step_end.actions[j];

        req += url_encode(sa.io->params["id"]) + ":" + url_encode(sa.action) + " ";
    }

    return req;
}

string ScenarioData::modifyRequest(IOBase *io)
{
    //request:
    // scenario modify input_123 name:bla visible:true room_name:bla room_type:bla cycle:true step:61000 output_0:actionbla output_1:actionbla step_end output_0:actionbla

    string req = "scenario modify " + io->params["id"] + " name:" + url_encode(name) + " ";

    if (visible && room)
    {
        req += "visible:true room_name:" + url_encode(room->name) + " ";
        req += "room_type:" + url_encode(room->type) + " ";
    }
    else
    {
        req += "visible:false ";
    }

    if (params["cycle"] == "true")
        req += "cycle:true ";
    else
        req += "cycle:false ";

    //Add each steps
    for (uint i = 0;i < steps.size();i++)
    {
        ScenarioStep &step = steps[i];

        req += "step:" + Utils::to_string(step.pause) + " ";

        for (uint j = 0;j < step.actions.size();j++)
        {
            ScenarioAction &sa = step.actions[j];

            req += url_encode(sa.io->params["id"]) + ":" + url_encode(sa.action) + " ";
        }
    }

    //End step
    req += "step_end ";
    for (uint j = 0;j < step_end.actions.size();j++)
    {
        ScenarioAction &sa = step_end.actions[j];

        req += url_encode(sa.io->params["id"]) + ":" + url_encode(sa.action) + " ";
    }

    return req;
}

void Scenario::createSchedule(sigc::slot<void, IOBase *> callback)
{
    /*
    string cmd = "scenario add_schedule " + ioScenario->params["id"];
    connection->SendCommand(cmd, [=](bool success, vector<string> result, void *user_data)
    {
        if (!success || result.size() != 3)
        {
            callback(nullptr);
            return;
        }

        //result should be:
        //scenario scenario_id_0 schedule_id:timerange_0

        if (result[1] != ioScenario->params["id"] ||
            !Utils::strStartsWith(result[2], "schedule_id:"))
        {
            callback(nullptr);
            return;
        }

        string id = result[2].substr(string("schedule_id:").length());
        double start_time = ecore_time_get();

        //We need to delay a bit because we have to wait for RoomModel to load the io id first
        timer = new EcoreTimer(0.05, [=]()
        {
            map<string, IOBase *>::const_iterator it = CalaosModel::Instance().getHome()->getCacheInputs().find(id);

            if (it == CalaosModel::Instance().getHome()->getCacheInputs().end())
            {
                if (ecore_time_get() - start_time >= 5.0)
                {
                    cErrorDom("scenario") << "I was not able to find input \'" << id << "\' for 5s! This is bad... Giving up.";
                    delete timer;

                    callback(nullptr);
                }
                else
                {
                    cDebugDom("scenario") << "Still waiting for input \'" << id << "\'....";
                }

                return;
            }

            DELETE_NULL(timer);

            ioSchedule = it->second;
            ioSchedule->io_deleted.connect([=]()
            {
                ioSchedule = nullptr;
            });

            callback(ioSchedule);
        });
    });
    */
}

void Scenario::deleteSchedule()
{
    /*
    string cmd = "scenario del_schedule " + ioScenario->params["id"];
    connection->SendCommand(cmd);
    ioSchedule = nullptr;
    */
}

void Scenario::setSchedules(TimeRangeInfos &tr)
{
    if (!ioSchedule)
    {
        cCriticalDom("scenario") << "called with ioPlage == null!";
        return;
    }

    cDebugDom("scenario") << "Saving Scenario schedule: ";
    cout << tr.toString();

    string cmd = "input " + ioSchedule->params["id"] + " plage set";

    auto addRange = [=,&cmd](const vector<TimeRange> &ranges, int day)
    {
        for (const TimeRange &t: ranges)
            cmd += " " + t.toProtoCommand(day);
    };

    addRange(tr.range_monday, 0);
    addRange(tr.range_tuesday, 1);
    addRange(tr.range_wednesday, 2);
    addRange(tr.range_thursday, 3);
    addRange(tr.range_friday, 4);
    addRange(tr.range_saturday, 5);
    addRange(tr.range_sunday, 6);

    //connection->SendCommand(cmd);

    //Send months
    stringstream ssmonth;
    ssmonth << tr.range_months;
    string str = ssmonth.str();
    std::reverse(str.begin(), str.end());

    /*
    cmd = "input " + ioSchedule->params["id"] + " plage months set " + str;
    connection->SendCommand(cmd);
    */
}

void ScenarioModel::createScenario(ScenarioData &data)
{
    /*
    string cmd = data.createRequest();
    connection->SendCommand(cmd);
    */
}

void ScenarioModel::modifyScenario(Scenario *sc)
{
    /*
    if (!sc || !sc->ioScenario) return;

    string cmd = sc->scenario_data.modifyRequest(sc->ioScenario);
    connection->SendCommand(cmd);
    */
}

void ScenarioModel::deleteScenario(Scenario *sc)
{
    /*
    if (!sc || !sc->ioScenario) return;

    string cmd = "scenario delete " + sc->ioScenario->params["id"];
    connection->SendCommand(cmd);
    */
}

void ScenarioModel::notifyScenarioAdd(const string &msgtype, const Params &evdata)
{
    cDebugDom("scenario") << "New scenario notif, start timer to load scenario data...";

    //We need to delay the load of the scenario because we have to wait for RoomModel to load the scenario id first
    EcoreTimer::singleShot(0.5, sigc::bind(sigc::mem_fun(*this, &ScenarioModel::notifyScenarioAddDelayed), msgtype, evdata));
}

void ScenarioModel::notifyScenarioAddDelayed(const string &msgtype, const Params &evdata)
{
    cDebugDom("scenario") << "New scenario, load data";
    vector<string> tok, split_id;
    split(msgtype, tok);

    if (tok.size() < 2) return;

    split(tok[1], split_id, ":", 2);

    if (split_id.size() < 2)
        return;

    map<string, IOBase *>::const_iterator it = CalaosModel::Instance().getHome()->getCacheInputs().find(split_id[1]);

    if (it == CalaosModel::Instance().getHome()->getCacheInputs().end())
    {
        cErrorDom("scenario") << "Unknown Input \'" << split_id[1] << "\' !";
        return;
    }

    Scenario *sc = new Scenario(connection);
    sc->ioScenario = it->second;
    sc->scenario_data.empty = false;
    sc->ioScenario->io_deleted.connect(sigc::bind(sigc::mem_fun(*this, &ScenarioModel::notifyScenarioDel), sc));
    sc->scenario_data.params.Add("id", split_id[1]);
    scenarios.push_back(sc);

    sc->load_done.connect(sigc::mem_fun(*this, &ScenarioModel::load_new_scenario_done));

    /*
    string cmd = "scenario get " + split_id[1];
    connection->SendCommand(cmd, [=](bool _success, vector<string> _result, void *_data)
    {
        sc->scenario_get_cb(_success, _result, _data);
        sc->load_done.emit(sc);
    });
    */
}

void ScenarioModel::load_new_scenario_done(Scenario *sc)
{
    cDebugDom("scenario") << "New scenario, load done, emit signal";
    scenario_new.emit(sc);
}

void ScenarioModel::notifyScenarioDel(Scenario *sc)
{
    if (!sc)
    {
        cErrorDom("scenario") << "sc is NULL!";
        return;
    }

    list<Scenario *>::iterator it;
    it = find(scenarios.begin(), scenarios.end(), sc);

    if (it == scenarios.end())
    {
        cErrorDom("scenario") << "Unknown scenario \'" << sc->ioScenario->params["id"] << "\' !";
        return;
    }

    scenario_del.emit(*it);

    delete *it;
    scenarios.erase(it);
}

void ScenarioModel::notifyScenarioChange(const string &msgtype, const Params &evdata)
{
    vector<string> tok, split_id;
    split(msgtype, tok);

    if (tok.size() < 2) return;

    split(tok[1], split_id, ":", 2);

    if (split_id.size() < 2)
        return;

    cDebug() << "scenario id: " << split_id[1] << " changed, broadcasting signal";

    list<Scenario *>::iterator it = scenarios.begin();
    for (;it != scenarios.end();it++)
    {
        Scenario *sc = *it;
        if (sc->ioScenario && sc->ioScenario->params["id"] == split_id[1])
        {
            cDebugDom("scenario") << "Reload scenario " << split_id[1];
            string cmd = "scenario get " + split_id[1];
            /*
            connection->SendCommand(cmd, [=](bool _success, vector<string> _result, void *_data)
            {
                //clear scenario data before reloading them again
                sc->scenario_data = ScenarioData();
                sc->scenario_data.params.Add("id", split_id[1]);
                sc->ioSchedule = nullptr;

                sc->scenario_get_cb(_success, _result, _data);
                scenario_change.emit(sc);
            });
            */

            break;
        }
    }
}

list<ScenarioSchedule> ScenarioModel::getScenarioForDate(struct tm scDate)
{
    list<ScenarioSchedule> retList;

    auto it = scenarios.begin();
    for (;it != scenarios.end();it++)
    {
        Scenario *sc = *it;

        cDebugDom("scenario") << "Checking scenario: " << sc->ioScenario->params["name"];

        if (!sc->isScheduled())
            continue;

        cDebugDom("scenario") << "Scenario schedule: ";
        cout << sc->ioSchedule->range_infos.toString();

        auto checkScenario = [=,&retList](int day)
        {
            cDebugDom("scenario") << "Checking day: " << day;
            vector<int> num;
            num = sc->ioSchedule->range_infos.isScheduledDate(scDate);

            for (uint i = 0;i < num.size();i++)
            {
                ScenarioSchedule s;
                s.scenario = sc;
                s.day = day;
                s.timeRangeNum = num[i];
                retList.push_back(s);
            }
        };

        switch (scDate.tm_wday)
        {
        case 1: checkScenario(TimeRange::MONDAY); break;
        case 2: checkScenario(TimeRange::TUESDAY); break;
        case 3: checkScenario(TimeRange::WEDNESDAY); break;
        case 4: checkScenario(TimeRange::THURSDAY); break;
        case 5: checkScenario(TimeRange::FRIDAY); break;
        case 6: checkScenario(TimeRange::SATURDAY); break;
        case 0: checkScenario(TimeRange::SUNDAY); break;
        default: break;
        }
    }

    retList.sort([](const ScenarioSchedule &a, const ScenarioSchedule &b)
    {
        long timea = 0, timeb = 0;

        vector<TimeRange> vtr = a.scenario->ioSchedule->range_infos.getRange(a.day);
        if (a.timeRangeNum >= 0 && a.timeRangeNum < (int)vtr.size())
            timea = vtr[a.timeRangeNum].getStartTimeSec();

        vtr = b.scenario->ioSchedule->range_infos.getRange(b.day);
        if (b.timeRangeNum >= 0 && b.timeRangeNum < (int)vtr.size())
            timeb = vtr[b.timeRangeNum].getStartTimeSec();

        return timea < timeb;
    });

    return retList;
}
