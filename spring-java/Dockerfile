FROM eclipse-temurin:21-jdk-jammy AS build
WORKDIR /bot
COPY .. .
RUN chmod +x mvnw
RUN ./mvnw package -Dmaven.test.skip=true

FROM eclipse-temurin:21-jre-jammy
WORKDIR /bot
COPY --from=build /bot/target/*.jar app.jar
EXPOSE 8080
ENTRYPOINT ["java", "-jar", "app.jar"]

