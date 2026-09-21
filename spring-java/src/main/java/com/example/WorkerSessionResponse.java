package com.example;

public record WorkerSessionResponse(String email, String apiKey, String apiSecret,
                                    java.util.List<BotConfigResponse> configs) {
}