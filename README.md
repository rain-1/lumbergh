# process supervisor

launch with `./supervisor <directory>`.

it will watch the directory for addition and removal of "service directories".

if a service directory `foo` is added it will launch the executable inside that directory with the same name and populate thath directory with stdout and stderr logfiles
if a service directory is removed it will kill that process

if you hit control-C on the supervisor it will broadcast that to every child, taking them all down.
