# Simple mechanism to convert between binary AFS privacy flags and the
# standard human-readable form.
module AFS
  module PrivacyFlags
    # Convert an integer privacy flags value to a string.
    def pf_to_s(ival)
      rv = "-----"
      if ((ival & STATUS_ANY) != 0)
	rv[0] = "S"
      elsif ((ival & STATUS_MEM) != 0)
	rv[0] = "s"
      end
      if ((ival & OWNED_ANY) != 0)
	rv[1] = "O"
      end
      if ((ival & MEMBER_ANY) != 0)
	rv[2] = "M"
      elsif ((ival & MEMBER_MEM) != 0)
	rv[2] = "m"
      end
      if ((ival & ADD_ANY) != 0)
	rv[3] = "A"
      elsif ((ival & ADD_MEM) != 0)
	rv[3] = "a"
      end
      if ((ival & REMOVE_MEM) != 0)
	rv[4] = "r"
      end
      return rv
    end

    # Convert a string privacy flags value to an integer.
    def s_to_pf(sval)
      rv = 0
      sval.each_byte do |x|
	case x.chr
	when 'S'
	  rv |= STATUS_ANY
	when 's'
	  rv |= STATUS_MEM
	when 'O'
	  rv |= OWNED_ANY
	when 'M'
	  rv |= MEMBER_ANY
	when 'm'
	  rv |= MEMBER_MEM
	when 'A'
	  rv |= ADD_ANY
	when 'a'
	  rv |= ADD_MEM
	when 'r'
	  rv |= REMOVE_MEM
	when '-'
	  nil
	else
	  raise AFS::AFSLibraryError, "invalid privacy flag '#{x.chr}'"
	end
      end
      return rv
    end
  end
end
