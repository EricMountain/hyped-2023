#!/usr/bin/env bash

set -euo pipefail

IMAGE_NAME="hyped_ros"
# I'd be tempted to remove this next line and the --name option on the docker run command-line
# It prevents the ability to run multiple containers off the same image in parallel
CONTAINER_NAME="hyped_ros"

if [ "$1" = "help" ] || [ -z "$1" ]; then
  echo "Usage: ./build.sh [container|pod]"
  echo "       container - run a ros2 shell in a container"
  echo "       pod       - run all configured containers"
  echo "       rebuild   - rebuild the docker image"
  exit 0
fi

if ! [ -x "$(command -v docker)" ]; then
  echo '[!] Error: docker is not installed.' >&2
  exit 1
fi

if ! [ -x "$(command -v docker-compose)" ]; then
  echo '[!] Error: docker-compose is not installed.' >&2
  exit 1
fi

image=$( docker images -q $IMAGE_NAME 2> /dev/null )
if [[ -z ${image} ]]; then
  echo "[!] Building image"
  docker build -t $IMAGE_NAME .
else 
  echo "[>] Image already built"
fi

if [ "$1" = "container" ]; then
  echo "[>] Running container"

# This block revives an old container if present. So even if we rebuild the image, we revive an old container spun
# up from an old image. This means that the process of updating requires:
# - git pull
# - ./docker.sh rebuild
# - kill the old container (docker kill)
# - remove the old container (docker rm)
# - ./docker.sh container

# Whereas if we just discard the container on exit, it becomes:
# - git pull
# - ./docker.sh rebuild
# - ./docker.sh container

#  container=$( docker ps -a -f name=$CONTAINER_NAME | grep $CONTAINER_NAME 2> /dev/null )
#  if [[ ! -z ${container} ]]; then
#    echo "[!] $CONTAINER_NAME container exists"
#    if [ "$( docker container inspect -f '{{.State.Running}}' $CONTAINER_NAME )" == "true" ]; then
#      echo "  - Container is already running"
#      echo "  - Attaching to container"
#      docker exec -it $CONTAINER_NAME bash
#    else
#      echo "  - Container is not running"
#      echo "  - Starting container"
#      docker start $CONTAINER_NAME > /dev/null
#      echo "    - Attaching to container"
#      docker exec -it $CONTAINER_NAME bash
#    fi
#  else
#    echo "[!] $CONTAINER_NAME container does not exist"
#    docker run -it -v $(pwd):/home/hyped -v /dev:/dev --name $CONTAINER_NAME $IMAGE_NAME bash

# I don't know if the /home/hyped mapping is necessary. The /dev one would be dangerous, but is thankfully ignored
# by Docker
  docker run --rm -it --name $CONTAINER_NAME $IMAGE_NAME bash

#  fi
elif [ "$1" = "pod" ]; then
  echo "[>] Running pod"
  docker-compose up
elif [ "$1" = "rebuild" ]; then
  echo "[>] Rebuilding image"
  docker build -t $IMAGE_NAME .
else
  echo "[!] Invalid argument"
fi


