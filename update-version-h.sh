user_agent_prefix=`grep m4_define configure.ac | sed "s/[][)(]/,/g" | grep user_agent_prefix  | cut -d , -f 6`

peer_id_prefix=`grep m4_define configure.ac | sed "s/[][)(]/,/g" | grep peer_id_prefix  | cut -d , -f 6`

if [ -d .svn ]; then
    svn_revision=`svnversion | sed -r 's/([0-9]+).*/\1/' `
else
    svn_revision=`grep -oh '\$Id: [^ ]\+ [0-9]\+' */*\.cc */*\.cpp */*\.[chm] | cut -d ' ' -f 3 | sort | tail -n 1 -`
fi

cat > libtransmission/version.h.new << EOF
#define PEERID_PREFIX         "${peer_id_prefix}"
#define USERAGENT_PREFIX      "${user_agent_prefix}"
#define SVN_REVISION          "${svn_revision}"
#define SVN_REVISION_NUM      ${svn_revision}
#define SHORT_VERSION_STRING  "${user_agent_prefix}"
#define LONG_VERSION_STRING   "${user_agent_prefix} (${svn_revision})"
EOF

cmp -s libtransmission/version.h.new libtransmission/version.h
cat libtransmission/version.h.new
if [ $? -eq 0 ] # test exit status of "cmp" command.
then
  echo "no update needed to version.h"
  rm libtransmission/version.h.new
else  
  echo "updated version.h"
  mv libtransmission/version.h.new libtransmission/version.h
fi
