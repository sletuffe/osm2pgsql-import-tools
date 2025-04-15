#!/bin/bash

# This script will help you after import in temporary tables with a temporary prefix used in osm2pgsql
# to delete old tables and remove them by new one, reducing downtime
# Of course, you need twice the disk space to use that !

. $(dirname $0)/../config.sh

if [ "$1" == "" ] || [ "$2" == "" ] || [ "$1" == "-h" ] ; then
  echo "  Usage : ./from-temporary-tables-during-import-to-production.sh <temporary_prefix> <production_prefix>"
  echo "  Example : ./from-temporary-tables-during-import-to-production.sh  planet_osm_temporary  planet_osm"
  exit 0
fi


for x in line nodes point polygon rels roads ways ; do 
  echo "drop table $2_$x cascade;" | psql $base_osm
  echo "ALTER TABLE $1_$x RENAME TO $2_$x;" | psql $base_osm
done

