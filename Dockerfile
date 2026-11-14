FROM gcc:13 AS build
WORKDIR /app
COPY . .
RUN gcc -Wall -pthread -O2 -o server main.c

FROM debian:bookworm-slim
WORKDIR /app
COPY --from=build /app/server .
COPY --from=build /app/www ./www
CMD ["./server", "-t", "4"]
