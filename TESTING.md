Testing the IPP Sample Code
===========================

The `test` directory contains a test configuration for the `ippserver` program.
To use this test configuration, run the `start-server.sh` script from the top-
level directory:

    test/start-server.sh

To modify its configuration:

- Change the parameters to ippserver: change the arguments in "start-server.sh".

- Change the printer configurations: edit the .conf files in the "print" and
  "print3d" subdirectories.
