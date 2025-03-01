#!/bin/bash

# This script is used to test the Docker image locally.
# It builds the Docker image, runs the server, and tests the connectivity.
# Application code test must be tested separately.

IMAGE_NAME="hibp_test_image"

docker build -t $IMAGE_NAME -f docker/Dockerfile .

# Check if the build was successful
if [ $? -ne 0 ]; then
  echo "Failed to build Docker image '$IMAGE_NAME'."
  exit 1
fi

export EXTRA_ARGS="--sha1-db=hibp_test.sha1.bin --ntlm-db=hibp_test.ntlm.bin --sha1t64-db=hibp_test.sha1t64.bin"

# Start server, mount database files from test/data
docker run -d --rm --name $IMAGE_NAME -p 8082:8082 -v $PWD/test/data:/data -e EXTRA_ARGS $IMAGE_NAME

# Wait for the server to start
COUNT=20
for i in $(seq 1 $COUNT); do
  echo "Waiting for server to start... $i"
  sleep 1
  if ! docker ps | grep -q "$IMAGE_NAME"; then
    echo "Docker container is not running."
    exit 1
  fi
  if timeout 2 curl -fso /dev/null http://127.0.0.1:8082/check/plain/running ; then
    echo "Server ready!"
    break
  fi
  if [ $i -eq $COUNT ]; then
    echo "Server did not start."
    docker logs $IMAGE_NAME
    docker stop $IMAGE_NAME
    exit 1
  fi
done

# Test a query
echo "Checking hash example"

hash="00001131628B741FF755AAC0E7C66D26A7C72082"
count=$(curl -s "http://127.0.0.1:8082/check/sha1/${hash}")

if [ "$count" -lt 1000 ]; then
  echo "Test failed: Hash count response is unexpected."
  exit 1
fi

echo "Test passed!"
docker stop $IMAGE_NAME
