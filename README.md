ruby-afs
--------

This is a very simple native extension for interacting with the
AFS protection server database. Perhaps it could be extended to
other AFS interfaces, but we don't have any need for that.

We use this module internally as part of a process that imports
AFS protection groups into LDAP. You probably shouldn't want to
use it, and for now we are not releasing it under a permissive
license so technically you can't. Feel free to read the code and
learn from it.

This was mostly written by Garrett Wollman in the late 2000s, with
only minimal effort expended since then.
