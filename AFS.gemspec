Gem::Specification.new do |s|
  s.name = 'AFS'
  s.version = '1.0'
  s.date = '2018-07-25'
  s.summary = "AFS protection server library"
  s.description = "A simple native extension for interfacing with the AFS protection server"
  s.authors = ["Garrett Wollman"]
  s.email = 'wollman@csail.mit.edu'
  s.files = ["lib/afs.rb", "lib/afs/group.rb", "lib/afs/privacy_flags.rb", "ext/AFS.c"]
  s.extensions = ["ext/extconf.rb"]
  s.licenses = ['Nonstandard']
  s.homepage = 'https://tig.csail.mit.edu/'
end
