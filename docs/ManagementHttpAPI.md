Homestead nodes can expose a management HTTP interface on port 8886 to allow various subscriber management operations. In order to configure this, set the `hs_hostname_mgmt` config option to `<homestead_management_address>:8886` and run `sudo service clearwater-infrastructure restart`.

## IMPU

    /impu/<public ID>/reg-data

Make a GET request to this URL to retrieve registration data for the specified subscriber. This is identical to the matching API call on the signaling interface, defined [here](https://github.com/Metaswitch/homestead/blob/dev/docs/homestead_api.md#impu---persistent-registration-state).

Responses:

  * 200 if successful, with an XML body as defined in the link above.
  * 502 if Homestead has been unable to contact the HSS.
  * 503 if Homestead is currently overloaded.
