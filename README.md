Those are shell scripts for importing and maintaining up to date an osm2pgsql schema of a postgresql database 
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

* Install osm2pgsql (and Osmosis if you need diff updates)

* Copy ./config/config-sample.sh to ./config.sh and adapt (or use some adapted to extract or full planet)

* Copy ./config/configuration-sample.txt to ./configuration.txt

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
Download the state.txt file a few minutes earlier than the osm file you imported and put it aside from "update-osm.sh" script

Tweak : If you are importing from planet file at planet.osm.org, this command : 
wget -q -O - http://planet.osm.org/planet/planet-latest.osm.bz2 | bunzip2 | head -n 10 | grep timestamp
should get you the timestamp of the file. If you are using the pbf file, timestamp is exactly the same, then get the state.txt here : http://planet.osm.org/replication/minute/ 
with a date just before.

Then put in your contrab a line like :
*/10 * * * * $PATH/update-osm.sh > some_log_file 2>&1

Tweak for debuging : in config.sh you can activate a more verbose output for more information
OR
run "./update-osm.sh -v" to force verbose mode


import of minutes diffs
-----------------------

configuration.txt : is the osmosis file (?) to configure diffs download

