err=0
count=0
while [ $err -eq 0 ]; do
  count=$((count+1))
  echo starting run number $count
  make check
  err=$?
done
