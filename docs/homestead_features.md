Homestead Feature List
======================

Homestead is a HSS-cache. It does not aim to provide the full functionality of an [HSS](http://www.etsi.org/deliver/etsi_ts/129300_129399/129336/11.01.00_60/ts_129336v110100p.pdf),
but instead to provide a high availability data store for Sprout to read subscriber
data from over a RESTful HTTP interface.

Authentication
--------------

In a Clearwater deployment, a Homestead node must be locked down using
firewalls such that it is only visible to other nodes, and not externally. Any
client able to send traffic to port 8888 of a Homestead node may perform any
operation on any data stored in Homestead, without providing any
authentication; allowing external access therefore presents a significant
security risk.

Subscriber data storage
-----------------------

Homestead provides a [RESTful HTTP interface](homestead_api.md) for storing and retrieving
the following subscriber data:

* Initial Filter Criteria
* Sip digests
* Serving S-CSCF
* Server capability information
