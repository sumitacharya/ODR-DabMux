/*
   Copyright (C) 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010,
   2011, 2012 Her Majesty the Queen in Right of Canada (Communications
   Research Center Canada)

   Copyright (C) 2015
   Matthias P. Braendli, matthias.braendli@mpb.li

   Implementation of FIG0
   */
/*
   This file is part of ODR-DabMux.

   ODR-DabMux is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the
   License, or (at your option) any later version.

   ODR-DabMux is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with ODR-DabMux.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "fig/FIG0.h"
#include "DabMultiplexer.h"

#define PACKED __attribute__ ((packed))

//=========== FIG 0/0 ===========


size_t FIG0_0::fill(uint8_t *buf, size_t max_size)
{
    if (max_size < 6) {
        return 0;
    }

    FIGtype0_0 *fig0_0;
    fig0_0 = (FIGtype0_0 *)buf;

    fig0_0->FIGtypeNumber = 0;
    fig0_0->Length = 5;
    fig0_0->CN = 0;
    fig0_0->OE = 0;
    fig0_0->PD = 0;
    fig0_0->Extension = 0;

    fig0_0->EId = htons(m_rti->ensemble->id);
    fig0_0->Change = 0;
    fig0_0->Al = 0;
    fig0_0->CIFcnt_hight = (m_rti->currentFrame / 250) % 20;
    fig0_0->CIFcnt_low = (m_rti->currentFrame % 250);

    return 6;
}

//=========== FIG 0/1 ===========
//

FIG0_1::FIG0_1(FIGRuntimeInformation *rti) : m_rti(rti)
{
    subchannelFIG0_1 = m_rti->ensemble->subchannels.end();
}

size_t FIG0_1::fill(uint8_t *buf, size_t max_size)
{
    size_t remaining = max_size;
    if (max_size < 6) {
        return 0;
    }
    FIGtype0_1 *fig0_1;
    figtype0_1 = (FIGtype0_1 *)buf;

    figtype0_1->FIGtypeNumber = 0;
    figtype0_1->Length = 1;
    figtype0_1->CN = 0;
    figtype0_1->OE = 0;
    figtype0_1->PD = 0;
    figtype0_1->Extension = 1;
    buf += 2;
    remaining -= 2;

    // Rotate through the subchannels until there is no more
    // space in the FIG0/1
    if (subchannelFIG0_1 == m_rti->ensemble->subchannels.end()) {
        subchannelFIG0_1 = m_rti->ensemble->subchannels.begin();
    }

    for (; subchannelFIG0_1 != m_rti->ensemble->subchannels.end();
            ++subchannelFIG0_1) {
        dabProtection* protection = &(*subchannelFIG0_1)->protection;

        if ( (protection->form == UEP && remaining < 3) ||
             (protection->form == EEP && remaining < 4) ) {
            break;
        }

        if (protection->form == UEP) {
            fig0_1subchShort =
                (FIG_01_SubChannel_ShortF*)buf;
            fig0_1subchShort->SubChId = (*subchannelFIG0_1)->id;

            fig0_1subchShort->StartAdress_high =
                (*subchannelFIG0_1)->startAddress / 256;
            fig0_1subchShort->StartAdress_low =
                (*subchannelFIG0_1)->startAddress % 256;

            fig0_1subchShort->Short_Long_form = 0;
            fig0_1subchShort->TableSwitch = 0;
            fig0_1subchShort->TableIndex =
                protection->uep.tableIndex;

            buf += 3;
            remaining -= 3;
            figtype0_1->Length += 3;
        }
        else if (protection->form == EEP) {
            fig0_1subchLong1 =
                (FIG_01_SubChannel_LongF*)buf;
            fig0_1subchLong1->SubChId = (*subchannelFIG0_1)->id;

            fig0_1subchLong1->StartAdress_high =
                (*subchannelFIG0_1)->startAddress / 256;
            fig0_1subchLong1->StartAdress_low =
                (*subchannelFIG0_1)->startAddress % 256;

            fig0_1subchLong1->Short_Long_form = 1;
            fig0_1subchLong1->Option = protection->eep.GetOption();
            fig0_1subchLong1->ProtectionLevel =
                protection->level;

            fig0_1subchLong1->Sub_ChannelSize_high =
                getSizeCu(*subchannelFIG0_1) / 256;
            fig0_1subchLong1->Sub_ChannelSize_low =
                getSizeCu(*subchannelFIG0_1) % 256;

            buf += 4;
            remaining -= 4;
            figtype0_1->Length += 4;
        }
    }

    return max_size - remaining;
}