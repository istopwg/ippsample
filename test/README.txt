Test ippserver Configuration
-----------------------------------

To start the ippserver instance:
  Simply run "./test/start-server.sh" from the folder one level higher than where
  this README.txt file resides.
  The "start-server.sh" script calls ippserver with a set of arguments to use the
  configuration in this directory.

To modify its configuration:
  Change the parameters to ippserver: change the arguments in "start-server.sh"
  Change the printer configurations: edit the .conf files in the "print" subdirectory.
