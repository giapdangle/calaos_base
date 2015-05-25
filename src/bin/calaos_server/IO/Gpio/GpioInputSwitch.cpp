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
#include <Utils.h>
#include <IOFactory.h>

#include "GpioInputSwitch.h"

using namespace Calaos;

REGISTER_INPUT(GpioInputSwitch)

GpioInputSwitch::GpioInputSwitch(Params &p):
    InputSwitch(p),
    gpioctrl(NULL)
{
    int gpio_nb;
    double debounce;
    bool active_low = false;

    Utils::from_string(get_param("gpio"), gpio_nb);
    Utils::from_string(get_param("debounce"), debounce);
    Utils::from_string(get_param("active_low"), active_low);

    gpioctrl = new GpioCtrl(gpio_nb, debounce);
    gpioctrl->setDirection("in");
    gpioctrl->setActiveLow(active_low);

    gpioctrl->setValueChanged([=] {
            gpioctrl->getVal(val);
            hasChanged();
            cInfoDom("Input") << "Input value changed, new value : " << value;
        });

    cInfoDom("Input") << "Create gpio input for gpio " << gpio_nb << " active_low : " << active_low;
}

GpioInputSwitch::~GpioInputSwitch()
{
    cDebugDom("input");
    delete gpioctrl;
}

bool GpioInputSwitch::readValue()
{
    cInfoDom("Input") << "Read Value : " << val;
    return val;
}
