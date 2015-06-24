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
#include "InPlageHoraire.h"
#include "ListeRule.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_INPUT(InPlageHoraire)
REGISTER_INPUT_USERTYPE(TimeRange, InPlageHoraire)

InPlageHoraire::InPlageHoraire(Params &p):
    Input(p),
    value(false)
{
    ListeRule::Instance().Add(this); //add this specific input to the EventLoop
    cDebugDom("input") << get_param("id") << ": Ok";

    set_param("visible", "false");
    set_param("gui_type", "time_range");

    months.set(); //set all months by default
}

InPlageHoraire::~InPlageHoraire()
{
    cDebugDom("input");
}

void InPlageHoraire::clear()
{
    plg_lundi.clear();
    plg_mardi.clear();
    plg_mercredi.clear();
    plg_jeudi.clear();
    plg_vendredi.clear();
    plg_samedi.clear();
    plg_dimanche.clear();
}

void InPlageHoraire::hasChanged()
{
    if (!isEnabled()) return;

    bool val = false;
    vector<TimeRange> *plage = NULL;

    struct tm *ctime = NULL;
    tzset(); //Force reload of timezone data
    time_t t = time(NULL);
    ctime = localtime(&t);

    switch (ctime->tm_wday)
    {
    case TimeRange::MONDAY: plage = &plg_lundi; break;
    case TimeRange::TUESDAY: plage = &plg_mardi; break;
    case TimeRange::WEDNESDAY: plage = &plg_mercredi; break;
    case TimeRange::THURSDAY: plage = &plg_jeudi; break;
    case TimeRange::FRIDAY: plage = &plg_vendredi; break;
    case TimeRange::SATURDAY: plage = &plg_samedi; break;
    case TimeRange::SUNDAY: plage = &plg_dimanche; break;
    default: break;
    }

    if (!plage)
        return;

    for (uint i = 0;i < plage->size();i++)
    {
        TimeRange &h = (*plage)[i];

        long start_time = h.getStartTimeSec(ctime->tm_year + 1900, ctime->tm_mon + 1, ctime->tm_mday);
        long end_time = h.getEndTimeSec(ctime->tm_year + 1900, ctime->tm_mon + 1, ctime->tm_mday);
        long cur = ctime->tm_hour * 3600 +
                   ctime->tm_min * 60 +
                   ctime->tm_sec;

        if (cur >= start_time && cur <= end_time)
            val = true;
    }

    if (val != value)
    {
        value = val;
        cInfoDom("input") << get_param("id") << ": Changed to " << (value?"true":"false");

        EmitSignalInput();

        EventManager::create(CalaosEvent::EventInputChanged,
                             { { "id", get_param("id") },
                               { "state", val?"true":"false" } });
    }
}

void InPlageHoraire::LoadPlage(TiXmlElement *node, vector<TimeRange> &plage)
{
    TiXmlHandle docHandle(node);
    TiXmlElement *cnode = docHandle.FirstChildElement().ToElement();
    for(; cnode; cnode = cnode->NextSiblingElement())
    {
        TimeRange h;
        if (cnode->Attribute("start_type"))
        {
            from_string(string(cnode->Attribute("start_type")), h.start_type);
            if (h.start_type < 0 || h.start_type > 3)
                h.start_type = TimeRange::HTYPE_NORMAL;
        }
        if (cnode->Attribute("start_offset"))
        {
            from_string(string(cnode->Attribute("start_offset")), h.start_offset);
            if (h.start_offset < 0) h.start_offset = -1;
            if (h.start_offset > 0) h.start_offset = 1;
        }
        if (cnode->Attribute("start_hour")) h.shour = cnode->Attribute("start_hour");
        if (cnode->Attribute("start_min")) h.smin = cnode->Attribute("start_min");
        if (cnode->Attribute("start_sec")) h.ssec = cnode->Attribute("start_sec");

        if (cnode->Attribute("end_type"))
        {
            from_string(string(cnode->Attribute("end_type")), h.end_type);
            if (h.end_type < 0 || h.end_type > 3)
                h.end_type = TimeRange::HTYPE_NORMAL;
        }
        if (cnode->Attribute("end_offset"))
        {
            from_string(string(cnode->Attribute("end_offset")), h.end_offset);
            if (h.end_offset < 0) h.end_offset = -1;
            if (h.end_offset > 0) h.end_offset = 1;
        }
        if (cnode->Attribute("end_hour")) h.ehour = cnode->Attribute("end_hour");
        if (cnode->Attribute("end_min")) h.emin = cnode->Attribute("end_min");
        if (cnode->Attribute("end_sec")) h.esec = cnode->Attribute("end_sec");

        stringstream sstart, sstop;
        if (h.start_type == TimeRange::HTYPE_NORMAL)
        {
            sstart << h.shour << ":" << h.smin << ":" << h.ssec;
        }
        else if (h.start_type == TimeRange::HTYPE_SUNRISE)
        {
            sstart << " Sunrise";
            if (h.shour != "0" || h.smin != "0" || h.ssec != "0")
            {
                if (h.start_offset > 0)
                    sstart << " +offset ";
                else
                    sstart << " -offset ";
                sstart << h.shour << ":" << h.smin << ":" << h.ssec;
            }
        }
        else if (h.start_type == TimeRange::HTYPE_SUNSET)
        {
            sstart << " Sunset";
            if (h.shour != "0" || h.smin != "0" || h.ssec != "0")
            {
                if (h.start_offset > 0)
                    sstart << " +offset ";
                else
                    sstart << " -offset ";
                sstart << h.shour << ":" << h.smin << ":" << h.ssec;
            }
        }
        else if (h.start_type == TimeRange::HTYPE_NOON)
        {
            sstart << " Noon";
            if (h.shour != "0" || h.smin != "0" || h.ssec != "0")
            {
                if (h.start_offset > 0)
                    sstart << " +offset ";
                else
                    sstart << " -offset ";
                sstart << h.shour << ":" << h.smin << ":" << h.ssec;
            }
        }

        if (h.end_type == TimeRange::HTYPE_NORMAL)
        {
            sstop << h.ehour << ":" << h.emin << ":" << h.esec;
        }
        else if (h.end_type == TimeRange::HTYPE_SUNRISE)
        {
            sstop << " Sunrise";
            if (h.ehour != "0" || h.emin != "0" || h.esec != "0")
            {
                if (h.end_offset > 0)
                    sstop << " +offset ";
                else
                    sstop << " -offset ";
                sstart << h.ehour << ":" << h.emin << ":" << h.esec;
            }
        }
        else if (h.end_type == TimeRange::HTYPE_SUNSET)
        {
            sstop << " Sunset";
            if (h.ehour != "0" || h.emin != "0" || h.esec != "0")
            {
                if (h.end_offset > 0)
                    sstop << " +offset ";
                else
                    sstop << " -offset ";
                sstop << h.ehour << ":" << h.emin << ":" << h.esec;
            }
        }
        else if (h.end_type == TimeRange::HTYPE_NOON)
        {
            sstop << " Noon";
            if (h.ehour != "0" || h.emin != "0" || h.esec != "0")
            {
                if (h.end_offset > 0)
                    sstop << " +offset ";
                else
                    sstop << " -offset ";
                sstop << h.ehour << ":" << h.emin << ":" << h.esec;
            }
        }

        cDebugDom("input") << "InPlageHoraire::LoadPlage(): Adding plage: "
                           << sstart.str() << " ===> " << sstop.str();

        plage.push_back(h);
    }
}

bool InPlageHoraire::LoadFromXml(TiXmlElement *pnode)
{
    TiXmlHandle docHandle(pnode);
    TiXmlElement *node = docHandle.FirstChildElement().ToElement();

    cDebugDom("input") << "InPlageHoraire::LoadFromXml(): Loading plage content";

    //try to load months
    if (pnode->Attribute("months"))
    {
        string m = pnode->Attribute("months");
        //reverse to have a left to right months representation
        std::reverse(m.begin(), m.end());

        try
        {
            bitset<12> mset(m);
            months = mset;
        }
        catch(...)
        {
            cErrorDom("input") << "Wrong parameters for months: " << m;
            cErrorDom("input") << "Setting all months to active";

            months.set(); //set all months by default
        }
    }

    for(; node; node = node->NextSiblingElement())
    {
        if (node->ValueStr() == "calaos:lundi")
            LoadPlage(node, plg_lundi);
        else if (node->ValueStr() == "calaos:mardi")
            LoadPlage(node, plg_mardi);
        else if (node->ValueStr() == "calaos:mercredi")
            LoadPlage(node, plg_mercredi);
        else if (node->ValueStr() == "calaos:jeudi")
            LoadPlage(node, plg_jeudi);
        else if (node->ValueStr() == "calaos:vendredi")
            LoadPlage(node, plg_vendredi);
        else if (node->ValueStr() == "calaos:samedi")
            LoadPlage(node, plg_samedi);
        else if (node->ValueStr() == "calaos:dimanche")
            LoadPlage(node, plg_dimanche);
    }

    return true;
}

void InPlageHoraire::SavePlage(TiXmlElement *node, string day, vector<TimeRange> &plage)
{
    if (plage.size() <= 0) return; //don't create node if empty

    TiXmlElement *day_node = new TiXmlElement(string("calaos:") + day);
    node->LinkEndChild(day_node);

    for (uint i = 0;i < plage.size();i++)
    {
        TiXmlElement *period_node = new TiXmlElement("calaos:plage");
        day_node->LinkEndChild(period_node);

        TimeRange &h = plage[i];

        period_node->SetAttribute("start_type", Utils::to_string(h.start_type));
        if (h.start_type == TimeRange::HTYPE_NORMAL)
        {
            period_node->SetAttribute("start_hour", h.shour);
            period_node->SetAttribute("start_min", h.smin);
            period_node->SetAttribute("start_sec", h.ssec);
        }
        else if (h.start_type == TimeRange::HTYPE_SUNRISE ||
                 h.start_type == TimeRange::HTYPE_SUNSET ||
                 h.start_type == TimeRange::HTYPE_NOON)
        {
            if (h.shour != "0" || h.smin != "0" || h.ssec != "0")
            {
                period_node->SetAttribute("start_hour", h.shour);
                period_node->SetAttribute("start_min", h.smin);
                period_node->SetAttribute("start_sec", h.ssec);
                period_node->SetAttribute("start_offset", h.start_offset);
            }
        }

        period_node->SetAttribute("end_type", Utils::to_string(h.end_type));
        if (h.end_type == TimeRange::HTYPE_NORMAL)
        {
            period_node->SetAttribute("end_hour", h.ehour);
            period_node->SetAttribute("end_min", h.emin);
            period_node->SetAttribute("end_sec", h.esec);
        }
        else if (h.end_type == TimeRange::HTYPE_SUNRISE ||
                 h.end_type == TimeRange::HTYPE_SUNSET ||
                 h.end_type == TimeRange::HTYPE_NOON)
        {
            if (h.ehour != "0" || h.emin != "0" || h.esec != "0")
            {
                period_node->SetAttribute("end_hour", h.ehour);
                period_node->SetAttribute("end_min", h.emin);
                period_node->SetAttribute("end_sec", h.esec);
                period_node->SetAttribute("end_offset", h.end_offset);
            }
        }
    }
}

bool InPlageHoraire::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *cnode = new TiXmlElement("calaos:input");
    node->LinkEndChild(cnode);

    for (int i = 0;i < get_params().size();i++)
    {
        string key, val;
        get_params().get_item(i, key, val);
        cnode->SetAttribute(key, val);
    }

    //Save months
    stringstream ssmonth;
    ssmonth << months;
    string str = ssmonth.str();
    std::reverse(str.begin(), str.end());

    cnode->SetAttribute("months", str);

    SavePlage(cnode, "lundi", plg_lundi);
    SavePlage(cnode, "mardi", plg_mardi);
    SavePlage(cnode, "mercredi", plg_mercredi);
    SavePlage(cnode, "jeudi", plg_jeudi);
    SavePlage(cnode, "vendredi", plg_vendredi);
    SavePlage(cnode, "samedi", plg_samedi);
    SavePlage(cnode, "dimanche", plg_dimanche);

    return true;
}
