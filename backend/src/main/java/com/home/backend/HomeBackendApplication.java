package com.home.backend;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.scheduling.annotation.EnableScheduling;

@SpringBootApplication
@EnableScheduling
public class HomeBackendApplication {

    public static void main(String[] args) {
        SpringApplication.run(HomeBackendApplication.class, args);
    }
}
