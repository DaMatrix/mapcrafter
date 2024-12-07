FROM devcontainer/cmake-cpp:latest

COPY --from=miclav/mapcrafter-blocks /blocks /blocks

RUN apt-get update -q=1 && \
  apt-get install -q=1 -y --no-install-recommends \
  clang-format \
  nginx-common \
  nginx

RUN rm /etc/nginx/sites-enabled/default && \
  ln -s /workspace/.devcontainer/mapcrafter.conf /etc/nginx/conf.d/mapcrafter.conf
