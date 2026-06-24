Those are shell scripts for importing and updating a postgresql database with osm2pgsql
+ tools around that 
+ SQL requests for custom indexes 
+ management of renderd tile expiry

osm2pgsql-import-tools
======================

Quick overview :
./config.sh --> common config file (write your own with ./config/* as models or examples)
./import.sh ---> to import from a .osm .osm.bz2 .pbf file or URL
./update-osm.sh ---> to import diff updates (see http://wiki.openstreetmap.org/wiki/Minutely_Mapnik )

side tools :
./render_list.sh --> to command renderd to render some zoom levels
./maintenance/time-spent-by-all-steps.sh --> benchmarks on time spent by different steps or update process (need activation)
./maintenance/from-temporary-tables-during-import-to-production.sh --> simple script to drop and move production tables after import to reduce downtime
./maintenance/gestion-des-access/creation-roles.sh --> postgres roles creation with read only access to osm2pgsql tables

Installation
============

* Install osm2pgsql (osm2pgsql-replication is bundled with it, used for diff updates)

* Copy ./config/config-sample.sh to ./config.sh and adapt (or use some adapted to extract or full planet)

* Copy some osm2pgsql style file from ./config/*.style to osm2pgsql-choosen.style (optionnal if you want the default one, then edit config.sh acordingly)

First import
============

Setup
-----
As the PG admin :
create a system user <user> :
    useradd <user>
create a pg role of the same name :
    createuser <user>
create a "gis" DB, owned by this role :
    createdb -E UTF8 -O <user> gis

Import
------
Log in shell with the <user> you created and do all other operations with that user.
From a local file :
./import.sh /truc/file.osm.bz2 (or pbf)

Update your database
====================
osm2pgsql-replication keeps track of where it left off directly in the database
(no state.txt file to manage by hand). First run only :
osm2pgsql-replication init -d <your db name>
(it auto-detects the starting point from the imported file's own timestamp)

Then put in your crontab a line like :
*/10 * * * * $PATH/update-osm.sh > some_log_file 2>&1

Tweak for debuging : in config.sh you can activate a more verbose output for more information
OR
run "./update-osm.sh -v" to force verbose mode

