#
# Ruby parts of AFS interface.
# At the moment, this is primarily AFS::Group#members_recursive,
# which provides support for supergroups.  (The C protection database
# interfaces don't expand supergroups.)
#
require 'AFS'

module AFS
  VERSION = 1.0
end

require "afs/group"
require "afs/privacy_flags"
