FROM alpine:latest as dependencies
RUN apk update
RUN apk add meson ninja
RUN apk add alpine-sdk linux-headers liburing-dev
RUN apk add sqlite-static sqlite-dev

FROM dependencies as builder
WORKDIR /usr/src/tomie
COPY . .
RUN meson build --native-file ./meson-native/static.txt
RUN meson compile -C build

FROM scratch as outputs
COPY --from=builder /usr/src/tomie/build/ .
