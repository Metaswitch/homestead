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

#ifndef IMS_SUBSCRIPTION_H__
#define IMS_SUBSCRIPTION_H__

#include <string>
#include <vector>
#include "charging_addresses.h"
#include "reg_state.h"


class ImsSubscription
{
public:
    // TODO actually implement the below
    // All member variables are pointers, as we want to have the possibility of having a
    // "partial" ImsSubscription that doesn't change the unset parts
    // We achieve this using NULL for an "unset" part

    // Constructor will create new objects from all the passed in variables
    // Destructor will delete all the non-NULL member variables

    // The XML to store
    std::string* service_profile;

    RegistrationState* reg_state;

    std::vector<std::string>* associated_impis;

    ChargingAddresses* charging_addresses;
};

#endif