#!/bin/bash
# update script for updating a postgresql osm2pgsql schema with diffs
# uses osm2pgsql-replication
# set -e

. $(dirname $0)/config.sh

if [ "$max_load" != "" ] ; then
  # check load average, if too high, exit
  if [ "$(grep '^[0-9]*' -o /proc/loadavg)" -ge "$max_load" ]; then
    exit
  fi
fi

# FIXME : I'm sure there is a better way to use parameters with bash scripts, but I'm lazy to search for it
if [ "$1" == "-v" ] || [ "$2" == "-v" ] ; then # To force verbosity for manual run without need to mess up with config file
  verbosity=1
fi

if [ $verbosity == 1 ] ; then
  dev_null_redirection=""
  set -x # prints command executed
else
  dev_null_redirection="> /dev/null"
fi

file_with_import_timeings=$work_dir/diff-update-timing

current_date="`date +%F-%R`"
message_log_file=$work_dir/replication-${current_date}.log
error_log_file=$work_dir/replication-${current_date}.err

# FIXME : pid file should go into a more appropriate zone such as /run/ on debian, but I don't really want to hard code it for debian only ;-)
script_lock_pid_file=$work_dir/script.pid

#create work_dir
mkdir -p $work_dir 2>/dev/null

function time_spent {
if [ $with_timeings == 0 ] ; then
  return;
fi
if [ "$1" == "start" ] ; then
        deb=`date +%s`
else
	date_now=$(date "+%Y-%m-%d %H:%M:%S")
        echo "$date_now,$2,$((`date +%s`-$deb))" >> $file_with_import_timeings
fi
}

#The pid file is older than 300 minutes (maybe make this a parameter ?), we consider something went wrong (serveur reboot, task stucked)
#we kill everything that could still be live
#This is however suboptimal, if some other process got that pid (like after a server crash, we might kill some innoncent process from the user running this script so : don't run it as root !
if [ -f $script_lock_pid_file ]; then
  if test `find $script_lock_pid_file -mmin +300` ; then
    kill -9 `cat $script_lock_pid_file` 2>/dev/null
    rm $script_lock_pid_file 2>/dev/null
  else # The previous running of that script is still running, exit
    exit
  fi
fi
#record the shell script's pid
echo $$ > $script_lock_pid_file

if [ ! -z "$osm2pgsql_expire_option" ]; then
  expire_options="$osm2pgsql_expire_option -o $osm2pgsql_expire_tile_list"
else
  expire_options=""
fi

#Download and apply diffs in one go, osm2pgsql-replication keeps track of where it
#left off in the database itself (no more state.txt to babysit)
time_spent start
eval $osm2pgsql_replication update --once -d $base_osm --max-diff-size $replication_max_diff_size -- $diff_osm2pgsql_options $expire_options $dev_null_redirection
replication_exit_code=$?
time_spent stop replication

if [ $replication_exit_code != 0 ] ; then
  echo "osm2pgsql-replication failed at importing diffs (exit code $replication_exit_code), more information if you enable verbosity." 1>&2
  rm $script_lock_pid_file
  if [ $verbosity == 1 ] ; then
    set +x
  fi
  exit 1
fi

#Tell tirex which tiles were impacted, either as an eager render request or as a lazy
#dirty mark, see "expire_method" in config.sh. rendering_styles_tiles_to_expire holds the
#tirex map names, comma separated (see /etc/tirex/renderer/mapnik/*.conf for the actual
#"name=" of each map)
if [ -s "$osm2pgsql_expire_tile_list" ] && [ ! -z "$rendering_styles_tiles_to_expire" ]; then
  time_spent start

  if [ "$expire_method" == "render" ] ; then
    #Active method: ask tirex-batch to actually (re)render every impacted tile right away,
    #even tiles nobody requested yet. Goes to the lowest priority bucket, so it never
    #preempts live traffic, but it can still burn rendering time/disk for nothing.
    awk -F'/' -v maps="$rendering_styles_tiles_to_expire" '{print "map="maps" x="$2" y="$3" z="$1}' $osm2pgsql_expire_tile_list \
      | eval $render_expired_prefix tirex-batch $dev_null_redirection
  else
    #Default "dirty" method: just mark the impacted metatiles (and their lower-zoom
    #parents, down to expire_minzoom) as expired on disk. No active rendering is queued;
    #mod_tile/tirex will only (re)render a tile the next time an actual visitor asks for it.
    for map in ${rendering_styles_tiles_to_expire//,/ } ; do
      eval $render_expired_prefix $project_dir/expiremeta.pl --map=$map --minzoom=$expire_minzoom < $osm2pgsql_expire_tile_list $dev_null_redirection
    done
  fi

  time_spent stop tile_expiry
  rm $osm2pgsql_expire_tile_list
fi

rm $script_lock_pid_file

if [ $verbosity == 1 ] ; then
  set +x
fi
