#
# Ruby parts of AFS interface.
# At the moment, this is primarily AFS::Group#members_recursive,
# which provides support for supergroups.  (The C protection database
# interfaces don't expand supergroups.)
#
module AFS
  class Group
    def members_recursive
      rv = []
      self.members do |member|
	if member.respond_to?(:members)
	  if block_given?
	    member.members_recursive {|m| yield m}
	  else
	    rv += member.members_recursive
	  end
	else
	  if block_given?
	    yield member
	  else
	    rv.push(member)
	  end
	end
      end
      return (block_given? ? nil : rv)
    end
  end
end
