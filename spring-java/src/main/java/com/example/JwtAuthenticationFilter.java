package com.example;

import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import org.springframework.security.authentication.UsernamePasswordAuthenticationToken;
import org.springframework.security.core.context.SecurityContextHolder;
import org.springframework.stereotype.Component;
import org.springframework.web.filter.OncePerRequestFilter;

import java.io.IOException;
import java.util.List;
import org.springframework.beans.factory.annotation.Value;

@Component
public class JwtAuthenticationFilter extends OncePerRequestFilter {
    private final JwtService jwtService;
    private final String workerServiceToken;

    public JwtAuthenticationFilter(JwtService jwtService, @Value("${app.worker.service-token}") String workerServiceToken) {
        this.jwtService = jwtService;
        this.workerServiceToken = workerServiceToken;
    }

    @Override
    protected void doFilterInternal(HttpServletRequest request, HttpServletResponse response, FilterChain chain)
            throws ServletException, IOException {
        String header = request.getHeader("Authorization");
        String internalToken = request.getHeader("X-Worker-Token");
        String workerEmail = request.getHeader("X-Worker-User");
        if (internalToken != null && internalToken.equals(workerServiceToken) && workerEmail != null && !workerEmail.isBlank()) {
            SecurityContextHolder.getContext().setAuthentication(
                new UsernamePasswordAuthenticationToken(workerEmail, null, List.of(() -> "ROLE_WORKER")));
            chain.doFilter(request, response);
            return;
        }
        String token = header != null && header.startsWith("Bearer ")
            ? header.substring(7)
            : request.getCookies() == null ? null : java.util.Arrays.stream(request.getCookies())
                .filter(cookie -> "ACCESS_TOKEN".equals(cookie.getName()))
                .map(jakarta.servlet.http.Cookie::getValue)
                .findFirst().orElse(null);
        if (token != null) {
            try {
            String email = jwtService.getEmail(token);
            String role = "worker".equals(jwtService.getScope(token)) ? "ROLE_WORKER" : "ROLE_USER";
                var authentication = new UsernamePasswordAuthenticationToken(email, null, List.of(() -> role));
                SecurityContextHolder.getContext().setAuthentication(authentication);
            } catch (RuntimeException ignored) {
                SecurityContextHolder.clearContext();
            }
        }
        chain.doFilter(request, response);
    }
}