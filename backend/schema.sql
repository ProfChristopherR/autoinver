-- ═══════════════════════════════════════════════════════════════════════════════
-- AUTOINVER v2.0 - SCHEMA COMPLETO SUPABASE
-- Ejecutar en SQL Editor de Supabase
-- ═══════════════════════════════════════════════════════════════════════════════

-- ───────────────────────────────────────────────────────────────────────────────
-- 1. TABLA PRINCIPAL: Datos de telemetría
-- ───────────────────────────────────────────────────────────────────────────────

create table if not exists autoinver_data (
  id            bigint generated always as identity primary key,
  created_at    timestamptz default now(),
  device_id     text not null default 'AUTOINVER',
  
  -- Sensores ambientales
  temp_c        float,
  hum_pct       float,
  
  -- Sensores de suelo y agua
  soil_adc      int,
  soil_pct      float,
  water_adc     int,
  water_pct     float,
  
  -- Actuadores
  servo1_pos    int,
  servo2_pos    int,
  pump_state    boolean default false,
  
  -- Comunicación
  rssi          int,
  snr           float,
  
  -- Metadata
  mode          text default 'AUTO',
  raw_payload   text,
  packet_count  int
);

-- Índices para queries frecuentes
create index idx_autoinver_data_created_at on autoinver_data(created_at desc);
create index idx_autoinver_data_device_id on autoinver_data(device_id);
create index idx_autoinver_data_time_range on autoinver_data(created_at, device_id);

-- ───────────────────────────────────────────────────────────────────────────────
-- 2. TABLA DE EVENTOS: Cambios de estado significativos
-- ───────────────────────────────────────────────────────────────────────────────

create table if not exists autoinver_events (
  id            bigint generated always as identity primary key,
  created_at    timestamptz default now(),
  device_id     text not null default 'AUTOINVER',
  
  event_type    text not null,  -- 'PUMP_ON', 'PUMP_OFF', 'VENT_OPEN', 'VENT_CLOSE', 'MODE_CHANGE', 'ALERT_LOW_WATER', etc.
  value         text,           -- valor asociado al evento
  old_value     text,           -- valor anterior (si aplica)
  source        text default 'AUTO',  -- 'AUTO', 'MANUAL', 'REMOTE', 'SYSTEM'
  
  -- Contexto del evento
  temp_c        float,
  soil_pct      float,
  water_pct     float
);

create index idx_events_created_at on autoinver_events(created_at desc);
create index idx_events_type on autoinver_events(event_type);

-- ───────────────────────────────────────────────────────────────────────────────
-- 3. TABLA DE COMANDOS: Cola de comandos pendientes
-- ───────────────────────────────────────────────────────────────────────────────

create table if not exists commands_queue (
  id            bigint generated always as identity primary key,
  created_at    timestamptz default now(),
  
  device_id     text not null default 'AUTOINVER',
  action        text not null,  -- 'BOMBA', 'S1', 'S2', 'MODE', 'CAL'
  value         text not null,  -- valor del comando
  
  status        text default 'PENDING',  -- 'PENDING', 'SENT', 'ACK', 'FAILED', 'EXPIRED'
  sent_at       timestamptz,
  ack_at        timestamptz,
  expires_at    timestamptz default (now() + interval '5 minutes'),
  
  requested_by  text default 'dashboard',  -- 'dashboard', 'auto', 'system'
  ip_address    inet
);

create index idx_commands_status on commands_queue(status, created_at);
create index idx_commands_device on commands_queue(device_id, status);

-- ───────────────────────────────────────────────────────────────────────────────
-- 4. TABLA DE CONFIGURACIÓN POR DISPOSITIVO
-- ───────────────────────────────────────────────────────────────────────────────

create table if not exists device_config (
  id            bigint generated always as identity primary key,
  updated_at    timestamptz default now(),
  
  device_id     text not null unique default 'AUTOINVER',
  
  -- Umbrales de control
  temp_thresh_1   float default 28.0,   -- °C - abrir ventilación 1
  temp_thresh_2   float default 32.0,   -- °C - abrir ventilación 2
  temp_hysteresis float default 1.5,    -- °C
  soil_thresh     float default 40.0,   -- % - umbral riego
  soil_hysteresis float default 10.0,   -- %
  water_min_pct   float default 20.0,   -- % - mínimo agua para riego
  pump_duration   int default 10000,    -- ms
  
  -- Calibración
  sharp_min_adc   int default 500,
  sharp_max_adc   int default 3500,
  
  -- Intervalos
  tx_interval_ms  int default 5000,
  sensor_interval_ms int default 2000
);

-- Insertar configuración default
insert into device_config (device_id) values ('AUTOINVER')
on conflict (device_id) do nothing;

-- ───────────────────────────────────────────────────────────────────────────────
-- 5. VISTA: Último estado por dispositivo
-- ───────────────────────────────────────────────────────────────────────────────

create or replace view latest_device_status as
select distinct on (device_id)
  device_id,
  created_at as last_seen,
  temp_c,
  hum_pct,
  soil_pct,
  water_pct,
  servo1_pos,
  servo2_pos,
  pump_state,
  rssi,
  mode
from autoinver_data
order by device_id, created_at desc;

-- ───────────────────────────────────────────────────────────────────────────────
-- 6. VISTA: Resumen de últimas 24 horas
-- ───────────────────────────────────────────────────────────────────────────────

create or replace view daily_summary as
select
  device_id,
  date_trunc('hour', created_at) as hour,
  avg(temp_c) as avg_temp,
  min(temp_c) as min_temp,
  max(temp_c) as max_temp,
  avg(hum_pct) as avg_hum,
  avg(soil_pct) as avg_soil,
  avg(water_pct) as avg_water,
  count(*) as packet_count,
  sum(case when pump_state then 1 else 0 end) as pump_on_count
from autoinver_data
where created_at > now() - interval '24 hours'
group by device_id, date_trunc('hour', created_at)
order by hour desc;

-- ═══════════════════════════════════════════════════════════════════════════════
-- SEGURIDAD: ROW LEVEL SECURITY
-- ═══════════════════════════════════════════════════════════════════════════════

-- Habilitar RLS en todas las tablas
alter table autoinver_data enable row level security;
alter table autoinver_events enable row level security;
alter table commands_queue enable row level security;
alter table device_config enable row level security;

-- Políticas para autoinver_data (lectura/escritura pública para demo)
-- NOTA: En producción, restringir por API key o usuario autenticado

create policy "Allow anon insert data"
  on autoinver_data for insert
  to anon
  with check (true);

create policy "Allow anon select data"
  on autoinver_data for select
  to anon
  using (true);

-- Políticas para events (solo lectura pública, insert via triggers)
create policy "Allow anon select events"
  on autoinver_events for select
  to anon
  using (true);

-- Políticas para commands_queue
create policy "Allow anon insert commands"
  on commands_queue for insert
  to anon
  with check (true);

create policy "Allow anon select commands"
  on commands_queue for select
  to anon
  using (true);

create policy "Allow anon update commands"
  on commands_queue for update
  to anon
  using (true);

-- Políticas para device_config (lectura pública, update restringido)
create policy "Allow anon select config"
  on device_config for select
  to anon
  using (true);

-- ═══════════════════════════════════════════════════════════════════════════════
-- TRIGGERS: Generación automática de eventos
-- ═══════════════════════════════════════════════════════════════════════════════

-- Función para detectar cambios de estado
 create or replace function detect_state_changes()
 returns trigger as $$
 declare
   prev_record record;
 begin
   -- Obtener registro anterior del mismo device
   select * into prev_record
   from autoinver_data
   where device_id = new.device_id and id < new.id
   order by id desc
   limit 1;
   
   -- Si no hay registro anterior, salir
   if prev_record is null then
     return new;
   end if;
   
   -- Detectar cambio de bomba
   if new.pump_state is distinct from prev_record.pump_state then
     insert into autoinver_events (device_id, event_type, value, old_value, source, temp_c, soil_pct, water_pct)
     values (
       new.device_id,
       case when new.pump_state then 'PUMP_ON' else 'PUMP_OFF' end,
       case when new.pump_state then 'ON' else 'OFF' end,
       case when prev_record.pump_state then 'ON' else 'OFF' end,
       new.mode,
       new.temp_c,
       new.soil_pct,
       new.water_pct
     );
   end if;
   
   -- Detectar cambio de modo
   if new.mode is distinct from prev_record.mode then
     insert into autoinver_events (device_id, event_type, value, old_value, source, temp_c, soil_pct, water_pct)
     values (
       new.device_id,
       'MODE_CHANGE',
       new.mode,
       prev_record.mode,
       'SYSTEM',
       new.temp_c,
       new.soil_pct,
       new.water_pct
     );
   end if;
   
   -- Detectar apertura/cierre de ventilación
   if new.servo1_pos is distinct from prev_record.servo1_pos then
     insert into autoinver_events (device_id, event_type, value, old_value, source, temp_c, soil_pct, water_pct)
     values (
       new.device_id,
       case when new.servo1_pos > prev_record.servo1_pos then 'VENT1_OPEN' else 'VENT1_CLOSE' end,
       new.servo1_pos::text,
       prev_record.servo1_pos::text,
       new.mode,
       new.temp_c,
       new.soil_pct,
       new.water_pct
     );
   end if;
   
   if new.servo2_pos is distinct from prev_record.servo2_pos then
     insert into autoinver_events (device_id, event_type, value, old_value, source, temp_c, soil_pct, water_pct)
     values (
       new.device_id,
       case when new.servo2_pos > prev_record.servo2_pos then 'VENT2_OPEN' else 'VENT2_CLOSE' end,
       new.servo2_pos::text,
       prev_record.servo2_pos::text,
       new.mode,
       new.temp_c,
       new.soil_pct,
       new.water_pct
     );
   end if;
   
   -- Alerta: nivel de agua bajo
   if new.water_pct < 15.0 and (prev_record.water_pct is null or prev_record.water_pct >= 15.0) then
     insert into autoinver_events (device_id, event_type, value, old_value, source, temp_c, soil_pct, water_pct)
     values (
       new.device_id,
       'ALERT_LOW_WATER',
       new.water_pct::text,
       prev_record.water_pct::text,
       'SYSTEM',
       new.temp_c,
       new.soil_pct,
       new.water_pct
     );
   end if;
   
   -- Alerta: temperatura extrema
   if new.temp_c > 40.0 and (prev_record.temp_c is null or prev_record.temp_c <= 40.0) then
     insert into autoinver_events (device_id, event_type, value, source, temp_c, soil_pct, water_pct)
     values (
       new.device_id,
       'ALERT_HIGH_TEMP',
       new.temp_c::text,
       'SYSTEM',
       new.temp_c,
       new.soil_pct,
       new.water_pct
     );
   end if;
   
   return new;
 end;
 $$ language plpgsql security definer;

-- Crear trigger
drop trigger if exists trg_state_changes on autoinver_data;
create trigger trg_state_changes
  after insert on autoinver_data
  for each row
  execute function detect_state_changes();

-- ═══════════════════════════════════════════════════════════════════════════════
-- FUNCIONES RPC
-- ═══════════════════════════════════════════════════════════════════════════════

-- Función para insertar batch de datos (desde gateway con buffer)
create or replace function insert_telemetry_batch(
  p_device_id text,
  p_records jsonb
)
returns int as $$
declare
  rec record;
  inserted_count int := 0;
begin
  for rec in select * from jsonb_array_elements(p_records)
  loop
    insert into autoinver_data (
      device_id, temp_c, hum_pct, soil_adc, soil_pct,
      water_adc, water_pct, servo1_pos, servo2_pos,
      pump_state, rssi, mode, raw_payload, packet_count
    ) values (
      p_device_id,
      (rec->>'temp_c')::float,
      (rec->>'hum_pct')::float,
      (rec->>'soil_adc')::int,
      (rec->>'soil_pct')::float,
      (rec->>'water_adc')::int,
      (rec->>'water_pct')::float,
      (rec->>'servo1_pos')::int,
      (rec->>'servo2_pos')::int,
      (rec->>'pump_state')::boolean,
      (rec->>'rssi')::int,
      rec->>'mode',
      rec->>'raw_payload',
      (rec->>'packet_count')::int
    );
    inserted_count := inserted_count + 1;
  end loop;
  
  return inserted_count;
end;
$$ language plpgsql security definer;

-- Función para obtener comandos pendientes (polling desde gateway)
create or replace function get_pending_commands(
  p_device_id text
)
returns table (
  id bigint,
  action text,
  value text,
  created_at timestamptz
) as $$
begin
  return query
  select 
    cq.id,
    cq.action,
    cq.value,
    cq.created_at
  from commands_queue cq
  where cq.device_id = p_device_id
    and cq.status = 'PENDING'
    and cq.expires_at > now()
  order by cq.created_at asc;
  
  -- Marcar como SENT
  update commands_queue
  set status = 'SENT', sent_at = now()
  where device_id = p_device_id
    and status = 'PENDING'
    and expires_at > now();
end;
$$ language plpgsql security definer;

-- Función para confirmar recepción de comando (ACK desde gateway)
create or replace function ack_command(
  p_command_id bigint,
  p_status text default 'ACK'
)
returns boolean as $$
begin
  update commands_queue
  set status = p_status,
      ack_at = now()
  where id = p_command_id;
  
  return found;
end;
$$ language plpgsql security definer;

-- ═══════════════════════════════════════════════════════════════════════════════
-- DATOS DE PRUEBA (opcional, descomentar para testing)
-- ═══════════════════════════════════════════════════════════════════════════════
/*
insert into autoinver_data (device_id, temp_c, hum_pct, soil_pct, water_pct, pump_state, rssi, mode)
values 
  ('AUTOINVER', 25.5, 65.0, 45.0, 80.0, false, -72, 'AUTO'),
  ('AUTOINVER', 26.2, 63.0, 42.0, 79.0, false, -70, 'AUTO'),
  ('AUTOINVER', 28.5, 58.0, 38.0, 78.0, true, -68, 'AUTO'),
  ('AUTOINVER', 29.0, 55.0, 52.0, 77.0, false, -75, 'AUTO');
*/

-- ═══════════════════════════════════════════════════════════════════════════════
-- HABILITAR REALTIME (para dashboard en tiempo real)
-- ═══════════════════════════════════════════════════════════════════════════════

-- Añadir tabla a publicación de realtime (ejecutar en psql o usar UI)
-- alter publication supabase_realtime add table autoinver_data;
-- alter publication supabase_realtime add table commands_queue;
-- alter publication supabase_realtime add table autoinver_events;
