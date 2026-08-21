FROM eclipse-temurin:21-jdk-jammy
WORKDIR /bot
COPY . .
RUN chmod +x mvnw
CMD ["./mvnw", "spring-boot:run"]

