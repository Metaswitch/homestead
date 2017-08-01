/**
 * @file subscriber_data.h structs to represent subscriber data.
 *
 * Copyright (C) Metaswitch Networks 2015
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef IMPLICIT_REG_SET_H__
#define IMPLICIT_REG_SET_H__

#include <string>
#include <vector>
#include "charging_addresses.h"
#include "reg_state.h"


class ImplicitRegistrationSet
{
    //TODO implementation, constructor, destructor, getters etc.
public:
    void update_service_profile(std::string new_profile);
    void update_reg_state(RegistrationState new_state);
    void add_impi(std::string impi);
    void remove_impi(std::string impi);
    void set_charging_addresses(ChargingAddresses new_addresses);

private:
    std::string _service_profile;
    RegistrationState _reg_state;
    std::vector<std::string> _associated_impis;
    ChargingAddresses _charging_addresses;
};

#endif