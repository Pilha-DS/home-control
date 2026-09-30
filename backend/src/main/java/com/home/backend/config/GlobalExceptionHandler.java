package com.home.backend.config;

import org.springframework.http.HttpStatus;
import org.springframework.http.ProblemDetail;
import org.springframework.web.bind.MethodArgumentNotValidException;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.RestControllerAdvice;

import com.home.backend.device.CommandBusyException;

/**
 * Converte excecoes de dominio em respostas HTTP claras no formato
 * RFC 7807 ({@code application/problem+json}).
 * <p>
 * Mantemos tambem a propriedade legada {@code error} com o codigo curto
 * (ex.: {@code acesso_negado}) para nao quebrar clientes existentes.
 */
@RestControllerAdvice
public class GlobalExceptionHandler {

    @ExceptionHandler(CommandBusyException.class)
    public ProblemDetail handleCommandBusy(CommandBusyException e) {
        ProblemDetail pd = ProblemDetail.forStatusAndDetail(
                HttpStatus.CONFLICT, "Ja existe um comando em andamento para este dispositivo.");
        pd.setTitle("comando_em_andamento");
        pd.setProperty("error", "comando_em_andamento");
        pd.setProperty("remainingMs", e.remainingMs());
        return pd;
    }

    @ExceptionHandler(IllegalArgumentException.class)
    public ProblemDetail handleIllegalArgument(IllegalArgumentException e) {
        String code = e.getMessage() == null ? "erro" : e.getMessage();
        HttpStatus status = switch (code) {
            case "acesso_negado", "admin_sem_acesso_dispositivos" -> HttpStatus.FORBIDDEN;
            case "dispositivo_nao_encontrado", "usuario_nao_encontrado" -> HttpStatus.NOT_FOUND;
            default -> HttpStatus.BAD_REQUEST;
        };
        ProblemDetail pd = ProblemDetail.forStatus(status);
        pd.setTitle(code);
        pd.setDetail(code);
        pd.setProperty("error", code);
        return pd;
    }

    @ExceptionHandler(MethodArgumentNotValidException.class)
    public ProblemDetail handleValidation(MethodArgumentNotValidException e) {
        String field = e.getBindingResult().getFieldErrors().stream()
                .findFirst().map(f -> f.getField()).orElse("campo");
        String code = "campo_invalido:" + field;
        ProblemDetail pd = ProblemDetail.forStatus(HttpStatus.BAD_REQUEST);
        pd.setTitle("campo_invalido");
        pd.setDetail(code);
        pd.setProperty("error", code);
        pd.setProperty("field", field);
        return pd;
    }
}
