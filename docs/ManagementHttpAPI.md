Homestead nodes can expose a management HTTP interface on port 8886 to allow various subscriber management operations. In order to configure this, set the `hs_hostname_mgmt` config option to `<homestead_management_address>:9886` and run `sudo service clearwater-infrastructure restart`.

## IMPU

    /impu/<public ID>/reg-data

Make a GET request to this URL to retrieve registration data for the specified subscriber. This is identical to the matching API call on the signaling interface, defined [here](https://github.com/Metaswitch/homestead/blob/dev/docs/homestead_api.md#impu---persistent-registration-state).

##

    /impu/

Make a GET request to this URL to list subscribers this Homestead knows about.

Responses:

  * 200 if successful, with a JSON body containing subscribers' public identities (max 1000).
  
  ```
  {
    "impus": [
      "sip:bob@example.com",
      "sip:alice@example.com",
      "sip:caleb@example.com"
    ]
  }
  ```
  
  * 500 if Homestead failed to contact Cassandra.
