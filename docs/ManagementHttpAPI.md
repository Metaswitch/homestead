Homestead nodes can expose a management HTTP interface on port 8886 to allow various subscriber management operations. In order to configure this, set the `hs_hostname_mgmt` config option to `<homestead_management_address>:8886` and run `sudo service clearwater-infrastructure restart`.

## IMPU

    /impu/<public ID>/reg-data

Make a GET request to this URL to retrieve registration data for the specified subscriber. This is identical to the matching API call on the signaling interface, defined [here](https://github.com/Metaswitch/homestead/blob/dev/docs/homestead_api.md#impu---persistent-registration-state).

Responses:

  * 200 if successful, with an XML body as defined in the link above.
  * 502 if Homestead has been unable to contact the HSS.
  * 503 if Homestead is currently overloaded.

---

    /impu/

Make a GET request to this URL to list subscribers this Homestead knows about.

Responses:

  * 200 if successful, with a JSON body containing subscribers' public identities (max 1000). The subscribers are returned in an arbitrary order, and if more than 1000 subscribers are present then an arbitrary selection of them are returned. If no subscribers are present then the list of `impus` is empty.
  
  ```
  {
    "impus": [
      "sip:bob@example.com",
      "sip:alice@example.com",
      "sip:caleb@example.com"
    ]
  }
  ```
  
  * 500 if Homestead has been unable to contact Cassandra.
