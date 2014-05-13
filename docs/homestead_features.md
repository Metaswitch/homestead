Homestead Feature List
======================

Homestead is a HSS-cache. It does not aim to provide the full functionality of an [HSS](http://www.etsi.org/deliver/etsi_ts/129300_129399/129336/11.01.00_60/ts_129336v110100p.pdf),
but instead to provide a high availability data store for Sprout to read subscriber
data from over a RESTful HTTP interface.

In traditional IMS terms, it is the data-caching part of an S-CSCF,
with Sprout providing the associated SIP-routing function of an S-CSCF.

Authentication
--------------

In a Clearwater deployment, a Homestead node must be locked down using
firewalls such that it is only visible to other nodes, and not externally. Any
client able to send traffic to port 8888 or port 8889 of a Homestead node may perform any
operation on any data stored in Homestead, without providing any
authentication; allowing external access therefore presents a significant
security risk.

Subscriber data storage
-----------------------

Homestead provides a [RESTful HTTP interface](homestead_api.md) for storing and retrieving
the following subscriber data:

* IMS subscriptions (Server-Assignment Requests)
* SIP-Digest or AKA authentication vectors (Multimedia-Auth Requests)
* Serving S-CSCF (User-Authorization/Location-Information requests)
* Server capability information (User-Authorization/Location-Information requests)

Homestead also supports receiving requests from the HSS:

* Push-Profile Requests (Updates cached IMS subscriptions)
* Registration-Termination Requests (Updates subscriber registration information and notifies the S-CSCF).

Bulk provisioning
-----------------

Homestead supports [bulk provisioning](https://github.com/Metaswitch/crest/blob/dev/docs/Bulk-Provisioning%20Numbers.md) a large set of subscribers from a CSV file when deployed without an external HSS, via a set of command line tools.

Scalability
-----------

Homestead is horizontally scalable. A cluster of Homestead nodes provides access to the same
underlying Cassandra cluster, allowing the load to be spread between nodes.
