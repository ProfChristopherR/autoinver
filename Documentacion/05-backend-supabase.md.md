
---




# Backend - Supabase

## Tabla: autoinver_data
sql
create table autoinver_data (
  id bigint generated always as identity primary key,
  created_at timestamptz default now(),
  device_id text not null,
  temp_c float,
  hum_pct float,
  soil_adc int,
  soil_pct float,
  water_adc int,
  water_pct float,
  servo1_pos int,
  servo2_pos int,
  pump_state boolean,
  rssi int,
  raw_payload text
);

-- Habilitar RLS
alter table autoinver_data enable row level security;

-- Politica: anon puede insertar (desde gateway)
create policy "Allow anon insert" on autoinver_data
  for insert to anon with check (true);

-- Politica: anon puede leer (desde dashboard)
create policy "Allow anon select" on autoinver_data
  for select to anon using (true);
## Tabla: autoinver_events (cambios de estado)
create table autoinver_events (
  id bigint generated always as identity primary key,
  created_at timestamptz default now(),
  device_id text not null,
  event_type text,  -- 'PUMP_ON', 'PUMP_OFF', 'S1_OPEN', 'S1_CLOSE', etc.
  value text,
  source text  -- 'AUTO' o 'MANUAL'
);
## Edge Function (opcional): procesar comandos

El gateway puede hacer POST directo a la REST API de Supabase sin Edge Function.

## Endpoint para insertar
POST https://TU-PROJECT.supabase.co/rest/v1/autoinver_data
Headers:
  apikey: TU-ANON-KEY
  Authorization: Bearer TU-ANON-KEY
  Content-Type: application/json
Body: JSON del paquete parseado
## Credenciales (AGENTE: crear proyecto en Supabase)

- Crear proyecto en supabase.com
    
- Copiar URL y anon public key
    
- Pegar en gateway: `supabaseUrl` y `supabaseKey`