#!/bin/sh
if [ $# -lt 7 ] ; then
    echo "USAGE: ./autorelease.sh [API_V4_URL] [PROJECT_URL]
    [PROJECT_ID] [TOKEN]
    [COMMIT_TAG] [JOB] [PROJECT_NAME]"
exit 1;
fi

CI_API_V4_URL=$1
CI_PROJECT_URL=$2
CI_PROJECT_ID=$3
CI_TOKEN=$4
CI_COMMIT_TAG=$5
ARTIFACTS_JOB=$6
CI_PROJECT_NAME=$7

res=`echo -e "curl --header \"PRIVATE-TOKEN: $CI_TOKEN\" $CI_API_V4_URL/projects/$CI_PROJECT_ID/releases/$CI_COMMIT_TAG -o /dev/null -s -w %{http_code}"| /bin/bash`

if [[ $res == "200" ]]; then
    eval $(echo -e "curl --request POST --header \"PRIVATE-TOKEN: $CI_TOKEN\" \
        --data name=\"$CI_PROJECT_NAME-$CI_COMMIT_TAG-artifacts.zip\" \
        --data url=\"$CI_PROJECT_URL/-/jobs/artifacts/$CI_COMMIT_TAG/download?job=$ARTIFACTS_JOB\"\
        $CI_API_V4_URL/projects/$CI_PROJECT_ID/releases/$CI_COMMIT_TAG/assets/links")
else
    eval $(echo -e "curl --header 'Content-Type: application/json' --header \
        \"PRIVATE-TOKEN: $CI_TOKEN\" --data '{ \"name\": \"$CI_COMMIT_TAG\",  \
        \"tag_name\": \"$CI_COMMIT_TAG\", \"description\": \"auto_release\",\
        \"assets\": { \"links\": [{ \"name\": \
        \"$CI_PROJECT_NAME-$CI_COMMIT_TAG-artifacts.zip\", \"url\": \
        \"$CI_PROJECT_URL/-/jobs/artifacts/$CI_COMMIT_TAG/download?job=$ARTIFACTS_JOB\"\
    }] } }' --request POST $CI_API_V4_URL/projects/$CI_PROJECT_ID/releases/")
fi
