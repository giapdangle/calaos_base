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
#include <Scenario.h>
#include <IPC.h>
#include <AutoScenario.h>
#include <EcoreTimer.h>
#include <IOFactory.h>

using namespace Calaos;

REGISTER_INPUT(Scenario)

Scenario::Scenario(Params &p):
    Input(p),
    Output(p),
    value(false),
    auto_scenario(NULL)
{
    cInfoDom("output") << "Scenario::Scenario(" << get_param("id") << "): Ok";

    set_param("gui_type", "scenario");

    if (Input::get_param("auto_scenario") != "")
    {
        auto_scenario = new AutoScenario(this);
        Input::setAutoScenario(true);
    }

    if (!Input::get_params().Exists("visible")) set_param("visible", "true");
}

Scenario::~Scenario()
{
    DELETE_NULL(auto_scenario);

    cInfoDom("output") << "Scenario::~Scenario(): Ok";
}

void Scenario::force_input_bool(bool v)
{
    if (!Input::isEnabled()) return;

    value = v;
    EmitSignalInput();

    string sig = "input ";
    sig += Input::get_param("id") + " ";
    if (v)
        sig += Utils::url_encode(string("state:true"));
    else
        sig += Utils::url_encode(string("state:false"));
    IPC::Instance().SendEvent("events", sig);

    //reset input value to 0 after 250ms (simulate button press/release)
    EcoreTimer::singleShot(0.250, [=]() { value = false; });
}

bool Scenario::set_value(bool val)
{
    if (!Input::isEnabled()) return true;

    force_input_bool(val);

    string sig = "output ";
    sig += Input::get_param("id") + " ";
    if (val)
        sig += Utils::url_encode(string("state:true"));
    else
        sig += Utils::url_encode(string("state:false"));
    IPC::Instance().SendEvent("events", sig);

    return true;
}
