/**
 * @file hss_cache.h Abstract class definition of an Hss Cache.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */
#ifndef HSS_CACHE_H_
#define HSS_CACHE_H_

#include "handlers.h"
#include "store.h"
#include <vector>
#include <string>
#include "ims_subscription.h"

class HssCache
{
public:
  virtual ~HssCache();

  // All of these methods are synchronous, and run on a thread that is OK to block
  // They return Store::Status (from cpp-common's Store) which is used to determine which callback to use
  // If they are getting/listing data, the data is put into the supplied datastructure

  // Give a set of public IDs (representing an implicit registration set) an associated private ID.
  // used for regdata task
  // only adds public id to impi_mapping table
  Store::Status put_associated_private_id(std::vector<std::string> impus, std::string default_public_id, std::string impi, int ttl);

  // Get the cached IMS subscription XML for a public identity.
  // Basically the equivalent of GetRegData in Cassandra
  Store::Status get_ims_subscription_xml(std::string impu, ImsSubscription& subscription);

  // Cache the IMS subscription XML
  // basically the equivalent of PutRegData in Cassandra
  Store::Status put_ims_subscription_xml(ImsSubscription subscription);

  // Deletes the specified impu entries from the impu table, and removes the first impu in the
  // list from the impi mapping table for these impis (the list is assumed to have the default at the front)
  // used for de-registrations
  Store::Status delete_public_ids(std::vector<std::string> impis, std::vector<std::string> impus);

  // Returns a list of all impus in the cache
  // used for listing impus
  Store::Status list_impus(std::vector<std::string>& impus);

  // Used for RTRs
  // just returns list of default impus for these impis
  Store::Status get_associated_primary_public_ids(std::vector<std::string> impis, std::vector<std::string>& impus);

  // As the method says:
  //  - for every impi that's mapped to this IRS, remove the default impu of the IRS from the impi mapping table
  //    - if we're deleting all the impis for this IRS, then we should remove the impu entries as well
  //    - if we're not, we need to remove the impi from the default impus entry in the impu table
  // - if delete_impi_mappings is true, also remove the entries in the impi_mapping table for these impis
  // Used for RTRs
  Store::Status dissociate_irs_from_impis(std::vector<std::string> impis, std::vector<std::string> impus, bool delete_impi_mappings);

  // If we just have charging functions and no profile xml:
  //    * look up the default impus for the given impi
  //    * update the cached ims subscription xml for all default and associated impus with new charging functions,
  //      and refresh the TTL
  // If we have profile xml,we need to lookup the default public ids for any IRS the IMPI is part of to determine
  // whether this PPR will change the default public id.
  //    * If it will, reject it
  //    * else update the saved ims subscription for the default and associated impus, and remove any old
  //    * impus that are no longer associated with this IRS
  Store::Status put_ppr_data(std::string impis, ImsSubscription ppr_data);

};

#endif
