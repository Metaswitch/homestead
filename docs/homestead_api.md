# Homestead - API Guide

Homestead provides a RESTful API. This is used by the Sprout component.
All access must go via this API, rather than directly to the database or HSS.

## IMPI

    `/impi/<private ID>/digest`

Make a GET request to this URL to retrieve the digest of the specified private ID

The URL takes an optional query parameter: `public_id=<public_id>` If specified a digest is only returned if the private ID is able to authenticate the public ID.

Response:

* 200 if the digest is found, returned as JSON: `{ "digest_ha1": "<DIGEST>" }`
* 404 if the digest is not found.

    `/impi/<private ID>/registration-status?impu=<impu>[&visited-network=<domain>][&auth-type=<type>]`

Make a GET request to this URL to request authorization of a registration request from the specified user.

The URL takes two optional query parameters. The visited-network parameter is used to specify the network providing SIP services to the SIP user. The auth-type parameter is used to determine the type of registration authorization required. It can take the values REG (REGISTRATION), DEREG (DEREGISTRATION) and CAPAB (REGISTRATION_AND_CAPABILITIES).

Response:

* 200 if the user is authorized, returned as JSON. The response will contain the HSS result code, and either the name of a server capable of handling the user, or a list of capabilities that will allow the interrogating server to pick a serving server for the user. This list of capabilities can be empty.
`{ "result-code": "2001",
   "scscf": "<server-name>" }`
`{ "result-code": "2001",
   "mandatory-capabilities": [1,2,3],
   "optional-capabilities": [4,5,6] }`
`{ "result-code": "2001",
   "mandatory-capabilties": [],
   "optional-capabilities": [] }`
* 403 if the user cannot be authorized.
* 404 if the user cannot be found.
* 500 if the HSS is overloaded.

## IMPU

    `/impu/<public ID>/location?[originating=true][&auth-type=CAPAB]`

Make a GET request to this URL to request the name of the server currently serving the specified user.

The URL takes two optional query parameters. The originating parameter can be set to 'true' to specify that we have an origating request. The auth-type parameter can be set to CAPAB (REGISTRATION_AND_CAPABILIIES) to request S-CSCF capabilities information.

Response:

* 200 if the user is authorized, returned as JSON. The response will contain the HSS result code, and either the name of a server capable of handling the user, or a list of capabilities that will allow the interrogating server to pick a serving server for the user. This list of capabilities can be empty.
`{ "result-code": "2001",
   "scscf": "<server-name>" }`
`{ "result-code": "2001",
   "mandatory-capabilities": [1,2,3],
   "optional-capabilities": [4,5,6] }`
`{ "result-code": "2001",
   "mandatory-capabilties": [],
   "optional-capabilities": [] }`
* 404 if the user cannot be found.
* 500 if the HSS is overloaded.

    `/impu/<public ID>?[&private_id=<private_id>]`

Make a GET request to this URL to retrieve the IMS subscription document for this public ID

The URL takes an optional privated_id query parameter. If specified this public ID will be used on any Server Assignment Request sent to the HSS.

Response:

* 200 if the public ID is found, returned as an IMSSubscription XML document.
* 404 if the public ID is not found.
